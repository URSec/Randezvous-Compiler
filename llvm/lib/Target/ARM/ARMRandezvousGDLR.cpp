//===- ARMRandezvousGDLR.cpp - ARM Randezvous Global Data Layout Randomization//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that randomizes the layout
// of global data regions for ARM machine code.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-gdlr"

#include "ARMRandezvousGDLR.h"
#include "ARMRandezvousOptions.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

STATISTIC(NumBytesInRodata, "Original Rodata size");
STATISTIC(NumBytesInData, "Original Data size");
STATISTIC(NumBytesInBss, "Original Bss size");
STATISTIC(NumGarbageObjects, "Number of pointer-sized garbage objects inserted");
STATISTIC(NumGarbageObjectsInRodata, "Number of pointer-sized garbage objects inserted in Rodata");
STATISTIC(NumGarbageObjectsInData, "Number of pointer-sized garbage objects inserted in Data");
STATISTIC(NumGarbageObjectsInBss, "Number of pointer-sized garbage objects inserted in Bss");
STATISTIC(NumTrapsEtched, "Number of trap instructions etched");

char ARMRandezvousGDLR::ID = 0;

ARMRandezvousGDLR::ARMRandezvousGDLR() : ModulePass(ID) {
}

StringRef
ARMRandezvousGDLR::getPassName() const {
  return "ARM Randezvous Global Data Layout Randomization Pass";
}

void
ARMRandezvousGDLR::getAnalysisUsage(AnalysisUsage & AU) const {
  // We need this to access MachineFunctions
  AU.addRequired<MachineModuleInfoWrapperPass>();

  AU.setPreservesCFG();
  ModulePass::getAnalysisUsage(AU);
}

void
ARMRandezvousGDLR::releaseMemory() {
  TrapBlocks.clear();
  TrapBlocksUnetched.clear();
  TrapBlocksEtched.clear();
  GarbageObjects.clear();
  GarbageObjectsEligibleForGlobalGuard.clear();
}

//
// Method: createGlobalGuardFunction()
//
// Description:
//   This method creates a function (both Function and MachineFunction) that
//   picks a garbage object as the global guard.  A garbage object is eligible
//   to be picked as the global guard if it is writable, has a size of at least
//   32 bytes, and aligns at a 32-byte boundary.  If no such garbage object is
//   available, this method also creates an eligible garbage object.
//
// Input:
//   M - A reference to the Module in which to create the function.
//
// Return value:
//   A pointer to the created Function.
//
Function *
ARMRandezvousGDLR::createGlobalGuardFunction(Module & M) {
  // Create types for the global guard function
  uint64_t PtrSize = M.getDataLayout().getPointerSize();
  LLVMContext & Ctx = M.getContext();
  PointerType * BlockAddrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
  PointerType * ParamTy = PointerType::getUnqual(BlockAddrTy);
  FunctionType * FuncTy = FunctionType::get(Type::getVoidTy(Ctx),
                                            { ParamTy, ParamTy }, false);

  // Create the global guard function
  FunctionCallee FC = M.getOrInsertFunction(GlobalGuardFuncName, FuncTy);
  Function * F = dyn_cast<Function>(FC.getCallee());
  assert(F != nullptr && "Global guard function has wrong type!");
  MachineModuleInfo & MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  MachineFunction & MF = MMI.getOrCreateMachineFunction(*F);

  // Set necessary attributes and properties
  F->setLinkage(GlobalVariable::LinkOnceAnyLinkage);
  if (!F->hasFnAttribute(Attribute::Naked)) {
    F->addFnAttr(Attribute::Naked);
  }
  if (!F->hasFnAttribute(Attribute::NoUnwind)) {
    F->addFnAttr(Attribute::NoUnwind);
  }
  if (!F->hasFnAttribute(Attribute::WillReturn)) {
    F->addFnAttr(Attribute::WillReturn);
  }
  using Property = MachineFunctionProperties::Property;
  if (!MF.getProperties().hasProperty(Property::NoVRegs)) {
    MF.getProperties().set(Property::NoVRegs);
  }

  // Generate a list of global guard candidates
  std::vector<GlobalValue *> GlobalGuardCandidates;
  if (!GarbageObjectsEligibleForGlobalGuard.empty()) {
    for (unsigned i = 0; i < RandezvousNumGlobalGuardCandidates; ++i) {
      uint64_t Idx = (*RNG)() % GarbageObjectsEligibleForGlobalGuard.size();
      GlobalGuardCandidates.push_back(GarbageObjectsEligibleForGlobalGuard[Idx]);
    }
  } else {
    // Have to manually create a garbage object eligible for the global guard
    ArrayType * GarbageObjectTy = ArrayType::get(BlockAddrTy, 32 / PtrSize);
    GlobalVariable * GV = new GlobalVariable(
      M, GarbageObjectTy, false, GlobalVariable::InternalLinkage,
      Constant::getNullValue(GarbageObjectTy), GarbageObjectNamePrefix
    );
    GV->setAlignment(MaybeAlign(32));
    GlobalGuardCandidates.push_back(GV);
  }

  // Create a basic block if not created
  if (F->empty()) {
    assert(MF.empty() && "Machine IR basic block already there!");

    // Build an IR basic block
    BasicBlock * BB = BasicBlock::Create(Ctx, "", F);
    IRBuilder<> IRB(BB);
    IRB.CreateRetVoid(); // At this point, what the IR basic block contains
                         // doesn't matter so just place a return there

    // Build machine IR basic block(s)
    const TargetInstrInfo * TII = MF.getSubtarget().getInstrInfo();
    MachineBasicBlock * MBB = MF.CreateMachineBasicBlock(BB);
    MachineBasicBlock * MBB2 = nullptr;
    MachineBasicBlock * MBB3 = nullptr;
    MachineBasicBlock * MBB4 = nullptr;
    MachineBasicBlock * RetMBB = MBB;
    MF.push_back(MBB);
    if (RandezvousRNGAddress != 0 && GlobalGuardCandidates.size() > 1) {
      // User provided an RNG address, so load a random index from the RNG
      if (ARM_AM::getT2SOImmVal(RandezvousRNGAddress) != -1) {
        // Use MOVi if the address can be encoded in Thumb modified constant
        BuildMI(MBB, DebugLoc(), TII->get(ARM::t2MOVi), ARM::R2)
        .addImm(RandezvousRNGAddress)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp()); // No 'S' bit
      } else {
        // Otherwise use MOVi16/MOVTi16 to encode lower/upper 16 bits of the
        // address
        BuildMI(MBB, DebugLoc(), TII->get(ARM::t2MOVi16), ARM::R2)
        .addImm(RandezvousRNGAddress & 0xffff)
        .add(predOps(ARMCC::AL));
        BuildMI(MBB, DebugLoc(), TII->get(ARM::t2MOVTi16), ARM::R2)
        .addReg(ARM::R2)
        .addImm((RandezvousRNGAddress >> 16) & 0xffff)
        .add(predOps(ARMCC::AL));
      }

      MBB2 = MF.CreateMachineBasicBlock(BB);
      MF.push_back(MBB2);
      MBB->addSuccessor(MBB2);
      MBB2->addSuccessor(MBB2);
      // LDRi12 R3, [R2, #0]
      BuildMI(MBB2, DebugLoc(), TII->get(ARM::t2LDRi12), ARM::R3)
      .addReg(ARM::R2)
      .addImm(0)
      .add(predOps(ARMCC::AL));
      // CMPi8 R3, #0
      BuildMI(MBB2, DebugLoc(), TII->get(ARM::t2CMPri))
      .addReg(ARM::R3)
      .addImm(0)
      .add(predOps(ARMCC::AL));
      // BEQ MBB2
      BuildMI(MBB2, DebugLoc(), TII->get(ARM::t2Bcc))
      .addMBB(MBB2)
      .addImm(ARMCC::EQ)
      .addReg(ARM::CPSR, RegState::Kill);

      MBB3 = MF.CreateMachineBasicBlock(BB);
      MF.push_back(MBB3);
      MBB2->addSuccessor(MBB3);
      // Prepare for runtime modulus
      if (ARM_AM::getT2SOImmVal(GlobalGuardCandidates.size()) != -1) {
        // Use MOVi if the number of global guard candidates can be encoded in
        // Thumb modified constant
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVi), ARM::R2)
        .addImm(GlobalGuardCandidates.size())
        .add(predOps(ARMCC::AL))
        .add(condCodeOp()); // No 'S' bit
      } else {
        // Otherwise use MOVi16/MOVTi16 to encode lower/upper 16 bits of the
        // number
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVi16), ARM::R2)
        .addImm(GlobalGuardCandidates.size() & 0xffff)
        .add(predOps(ARMCC::AL));
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVTi16), ARM::R2)
        .addReg(ARM::R2)
        .addImm((GlobalGuardCandidates.size() >> 16) & 0xffff)
        .add(predOps(ARMCC::AL));
      }
      // UDIV R12, R3, R2
      BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2UDIV), ARM::R12)
      .addReg(ARM::R3)
      .addReg(ARM::R2)
      .add(predOps(ARMCC::AL));
      // MLS R3, R2, R12, R3
      BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MLS), ARM::R3)
      .addReg(ARM::R2)
      .addReg(ARM::R12)
      .addReg(ARM::R3)
      .add(predOps(ARMCC::AL));

      MBB4 = MF.CreateMachineBasicBlock(BB);
      MF.push_back(MBB4);
      MBB3->addSuccessor(MBB4);
      RetMBB = MBB4;
      for (unsigned i = 0; i < GlobalGuardCandidates.size() - 1; ++i) {
        // SUBri12 R2, R2, #1
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2SUBri12), ARM::R2)
        .addReg(ARM::R2)
        .addImm(1)
        .add(predOps(ARMCC::AL));
        // CMPrr R3, R2
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::tCMPr))
        .addReg(ARM::R3)
        .addReg(ARM::R2)
        .add(predOps(ARMCC::AL));
        // IT EQ
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2IT))
        .addImm(ARMCC::EQ)
        .addImm(2);
        // MOVi16 R12, @GlobalGuardCandidates[i]_lo
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVi16), ARM::R12)
        .addGlobalAddress(GlobalGuardCandidates[i], 0, ARMII::MO_LO16)
        .add(predOps(ARMCC::EQ, ARM::CPSR));
        // MOVTi16 R12, @GlobalGuardCandidates[i]_hi
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVTi16), ARM::R12)
        .addReg(ARM::R12)
        .addGlobalAddress(GlobalGuardCandidates[i], 0, ARMII::MO_HI16)
        .add(predOps(ARMCC::EQ, ARM::CPSR));
        // B MBB4
        BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2B))
        .addMBB(MBB4)
        .add(predOps(ARMCC::EQ, ARM::CPSR));
      }
      // MOVi16 R12, @GlobalGuardCandidates[last]_lo
      BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVi16), ARM::R12)
      .addGlobalAddress(GlobalGuardCandidates.back(), 0, ARMII::MO_LO16)
      .add(predOps(ARMCC::AL));
      // MOVTi16 R12, @GlobalGuardCandidates[last]_hi
      BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2MOVTi16), ARM::R12)
      .addReg(ARM::R12)
      .addGlobalAddress(GlobalGuardCandidates.back(), 0, ARMII::MO_HI16)
      .add(predOps(ARMCC::AL));
      // B MBB4
      BuildMI(MBB3, DebugLoc(), TII->get(ARM::t2B))
      .addMBB(MBB4)
      .add(predOps(ARMCC::AL));
    } else {
      // Pick a static global guard
      uint64_t Idx = (*RNG)() % GlobalGuardCandidates.size();
      // MOVi16 R12, @GlobalGuardCandidates[Idx]_lo
      BuildMI(MBB, DebugLoc(), TII->get(ARM::t2MOVi16), ARM::R12)
      .addGlobalAddress(GlobalGuardCandidates[Idx], 0, ARMII::MO_LO16)
      .add(predOps(ARMCC::AL));
      // MOVTi16 R12, @GlobalGuardCandidates[Idx]_hi
      BuildMI(MBB, DebugLoc(), TII->get(ARM::t2MOVTi16), ARM::R12)
      .addReg(ARM::R12)
      .addGlobalAddress(GlobalGuardCandidates[Idx], 0, ARMII::MO_HI16)
      .add(predOps(ARMCC::AL));
    }

    // STRi12 R12, [R0, #0]
    BuildMI(RetMBB, DebugLoc(), TII->get(ARM::t2STRi12))
    .addReg(ARM::R12)
    .addReg(ARM::R0)
    .addImm(0)
    .add(predOps(ARMCC::AL));
    // ADDri12 R12, R12, #32
    BuildMI(RetMBB, DebugLoc(), TII->get(ARM::t2ADDri12), ARM::R12)
    .addReg(ARM::R12)
    .addImm(32)
    .add(predOps(ARMCC::AL));
    // STRi12 R12, [R1, #0]
    BuildMI(RetMBB, DebugLoc(), TII->get(ARM::t2STRi12))
    .addReg(ARM::R12)
    .addReg(ARM::R1)
    .addImm(0)
    .add(predOps(ARMCC::AL));
    // BX_RET
    BuildMI(RetMBB, DebugLoc(), TII->get(ARM::tBX_RET))
    .add(predOps(ARMCC::AL));
  }

  // Add the global guard function to @llvm.used
  appendToUsed(M, { F });

  return F;
}

//
// Method: insertGarbageObjects()
//
// Description:
//   This method inserts a specified number of pointer-sized garbage objects
//   into the containing Module of a given GlobalVariable and places the
//   garbage objects before the GlobalVariable.
//
// Inputs:
//   GV          - A reference to a GlobalVariable before which to insert
//                 garbage objects.
//   NumGarbages - The number of pointer-sized garbage objects to insert.
//
void
ARMRandezvousGDLR::insertGarbageObjects(GlobalVariable & GV,
                                        uint64_t NumGarbages) {
  Module & M = *GV.getParent();

  //
  // Instead of creating N pointer-sized garbage objects, we create
  // N / (32 / pointer size) garbage array objects of (32 / pointer size)
  // elements (plus a remainder garbage array object if N % (32 / pointer size)
  // is not zero)
  //

  // Create types for the garbage objects
  uint64_t PtrSize = M.getDataLayout().getPointerSize();
  LLVMContext & Ctx = M.getContext();
  PointerType * BlockAddrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
  ArrayType * GarbageObjectTy = ArrayType::get(BlockAddrTy, 32 / PtrSize);
  ArrayType * RemainderTy = ArrayType::get(BlockAddrTy,
                                           NumGarbages % (32 / PtrSize));

  uint64_t RemainingSize = NumGarbages * PtrSize;
  while (RemainingSize > 0) {
    uint64_t ObjectSize = RemainingSize < 32 ? RemainingSize : 32;
    uint64_t ObjectAlign = RemainingSize < 32 ? PtrSize : 32;
    ArrayType * ObjectTy = RemainingSize < 32 ? RemainderTy : GarbageObjectTy;

    // Create an initializer for the garbage object
    Constant * Initializer = nullptr;
    if (GV.hasInitializer() && GV.getInitializer()->isZeroValue()) {
      // GV is in BSS, so initialize the garbage object with zeros
      Initializer = Constant::getNullValue(ObjectTy);
    } else if (EnableRandezvousGRBG && !TrapBlocks.empty()) {
      // Initialize the garbage object with addresses of random trap blocks
      std::vector<Constant *> InitArray;
      for (uint64_t i = 0; i < ObjectSize / PtrSize; ++i) {
        uint64_t Idx = (*RNG)() % TrapBlocks.size();
        const BasicBlock * BB = TrapBlocks[Idx]->getBasicBlock();
        InitArray.push_back(BlockAddress::get(const_cast<BasicBlock *>(BB)));
      }
      Initializer = ConstantArray::get(ObjectTy, InitArray);
    } else {
      // Initialize the garbage object with random values that have the LSB
      // set; this serves as the Thumb bit
      std::vector<Constant *> InitArray;
      for (uint64_t i = 0; i < ObjectSize / PtrSize; ++i) {
        InitArray.push_back(Constant::getIntegerValue(BlockAddrTy,
                                                      APInt(8 * PtrSize,
                                                            (*RNG)() | 0x1)));
      }
      Initializer = ConstantArray::get(ObjectTy, InitArray);
    }

    // Create the garbage object and insert it before GV
    GlobalVariable * GarbageObject = new GlobalVariable(
      M, ObjectTy, GV.isConstant(), GlobalVariable::InternalLinkage,
      Initializer, GarbageObjectNamePrefix, &GV
    );
    GarbageObject->setAlignment(MaybeAlign(ObjectAlign));
    NumGarbageObjects += ObjectSize / PtrSize;
    if (GarbageObject->isConstant()) {
      NumGarbageObjectsInRodata += ObjectSize / PtrSize;
    } else if (Initializer->isZeroValue()) {
      NumGarbageObjectsInBss += ObjectSize / PtrSize;
    } else {
      NumGarbageObjectsInData += ObjectSize / PtrSize;
    }

    // Keep track of the garbage object
    GarbageObjects.push_back(GarbageObject);
    if (EnableRandezvousGlobalGuard && !GarbageObject->isConstant() &&
        ObjectSize == 32 && !Initializer->isZeroValue()) {
      GarbageObjectsEligibleForGlobalGuard.push_back(GarbageObject);
    }

    // Etch (the lower 16 bits of) the garbage object's address onto a trap
    // instruction so that it will not be GC'd away
    if (!TrapBlocksUnetched.empty()) {
      MachineBasicBlock * TrapBlock = TrapBlocksUnetched.back();
      assert(!TrapBlock->empty() && "Invalid trap block!");
      MachineInstr & TrapInst = TrapBlock->front();
      assert(TrapInst.getOpcode() == ARM::t2UDF_ga && "Invalid trap block!");

      TrapBlocksUnetched.pop_back();
      TrapInst.getOperand(0).ChangeToGA(GarbageObject, 0, ARMII::MO_LO16);
      TrapBlocksEtched.push_back(TrapBlock);
      ++NumTrapsEtched;
    } else if (!TrapBlocks.empty()) {
      errs() << "[GDLR] All trap blocks etched!\n";
    }

    RemainingSize -= ObjectSize;
  }
}

//
// Method: runOnModule()
//
// Description:
//   This method is called when the PassManager wants this pass to transform
//   the specified Module.  This method shuffles the order of global variables
//   within the module and inserts garbage objects to fill the global data
//   regions.
//
// Input:
//   M - A reference to the Module to transform.
//
// Output:
//   M - The transformed Module.
//
// Return value:
//   true  - The Module was transformed.
//   false - The Module was not transformed.
//
bool
ARMRandezvousGDLR::runOnModule(Module & M) {
  MachineModuleInfo & MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  Twine RNGName = getPassName() + "-" + Twine(RandezvousGDLRSeed);
  RNG = M.createRNG(RNGName.str());

  // Find trap blocks inserted by CLR
  for (Function & F : M) {
    MachineFunction * MF = MMI.getMachineFunction(F);
    if (MF != nullptr) {
      for (MachineBasicBlock & MBB : *MF) {
        if (MBB.isRandezvousTrapBlock()) {
          TrapBlocks.push_back(&MBB);
          TrapBlocksUnetched.push_back(&MBB);
        }
      }
    }
  }

  // First, classify all the globals into three categories
  std::vector<GlobalVariable *> RodataGVs;
  std::vector<GlobalVariable *> DataGVs;
  std::vector<GlobalVariable *> BssGVs;
  for (GlobalVariable & GV : M.globals()) {
    if (GV.isConstant()) {
      if (!GV.hasSection() || GV.getSection().startswith(".rodata")) {
        RodataGVs.push_back(&GV);
      } else {
        errs() << "[GDLR] Ignore rodata GV: " << GV << "\n";
      }
    } else if (GV.hasInitializer()) {
      if (GV.getInitializer()->isZeroValue()) {
        if (!GV.hasSection() || GV.getSection().startswith(".bss")) {
          BssGVs.push_back(&GV);
        } else {
          errs() << "[GDLR] Ignore bss GV: " << GV << "\n";
        }
      } else {
        if (!GV.hasSection() || GV.getSection().startswith(".data")) {
          DataGVs.push_back(&GV);
        } else {
          errs() << "[GDLR] Ignore data GV: " << GV << "\n";
        }
      }
    } else {
      errs() << "[GDLR] Ignore external GV: " << GV << "\n";
    }
  }

  // Second, calculate how much space each category of globals has taken up
  const DataLayout & DL = M.getDataLayout();
  uint64_t TotalRodataSize = 0, TotalDataSize = 0, TotalBssSize = 0;
  for (GlobalVariable * GV : RodataGVs) {
    TotalRodataSize += DL.getTypeAllocSize(GV->getType()->getElementType());
  }
  for (GlobalVariable * GV : DataGVs) {
    TotalDataSize += DL.getTypeAllocSize(GV->getType()->getElementType());
  }
  for (GlobalVariable * GV : BssGVs) {
    TotalBssSize += DL.getTypeAllocSize(GV->getType()->getElementType());
  }
  NumBytesInRodata = TotalRodataSize;
  NumBytesInData = TotalDataSize;
  NumBytesInBss = TotalBssSize;

  if (!EnableRandezvousGDLR) {
    return false;
  }

  assert(TotalRodataSize <= RandezvousMaxRodataSize && "Rodata size exceeds the limit!");
  assert(TotalDataSize <= RandezvousMaxDataSize && "Data size exceeds the limit!");
  assert(TotalBssSize <= RandezvousMaxBssSize && "Bss size exceeds the limit!");

  // Third, shuffle the order of globals
  SymbolTableList<GlobalVariable> & GlobalList = M.getGlobalList();
  llvm::shuffle(RodataGVs.begin(), RodataGVs.end(), *RNG);
  llvm::shuffle(DataGVs.begin(), DataGVs.end(), *RNG);
  llvm::shuffle(BssGVs.begin(), BssGVs.end(), *RNG);
  for (GlobalVariable * GV : RodataGVs) {
    GlobalList.remove(GV);
  }
  for (GlobalVariable * GV : DataGVs) {
    GlobalList.remove(GV);
  }
  for (GlobalVariable * GV : BssGVs) {
    GlobalList.remove(GV);
  }
  for (GlobalVariable * GV : RodataGVs) {
    GlobalList.push_back(GV);
  }
  for (GlobalVariable * GV : DataGVs) {
    GlobalList.push_back(GV);
  }
  for (GlobalVariable * GV : BssGVs) {
    GlobalList.push_back(GV);
  }

  // Fourth, determine the numbers of pointer-sized garbage objects
  uint64_t PtrSize = DL.getPointerSize();
  uint64_t NumGrbgInRodata = (RandezvousMaxRodataSize - TotalRodataSize) / PtrSize;
  uint64_t NumGrbgInData = (RandezvousMaxDataSize - TotalDataSize) / PtrSize;
  uint64_t NumGrbgInBss = (RandezvousMaxBssSize - TotalBssSize) / PtrSize;
  uint64_t SumSharesForRodata = 0;
  uint64_t SumSharesForData = 0;
  uint64_t SumSharesForBss = 0;
  std::vector<uint64_t> SharesForRodata(RodataGVs.size());
  std::vector<uint64_t> SharesForData(DataGVs.size());
  std::vector<uint64_t> SharesForBss(BssGVs.size());
  for (uint64_t i = 0; i < RodataGVs.size(); ++i) {
    SharesForRodata[i] = (*RNG)() & 0xffffffff; // Prevent overflow
    SumSharesForRodata += SharesForRodata[i];
  }
  for (uint64_t i = 0; i < DataGVs.size(); ++i) {
    SharesForData[i] = (*RNG)() & 0xffffffff;   // Prevent overflow
    SumSharesForData += SharesForData[i];
  }
  for (uint64_t i = 0; i < BssGVs.size(); ++i) {
    SharesForBss[i] = (*RNG)() & 0xffffffff;    // Prevent overflow
    SumSharesForBss += SharesForBss[i];
  }
  for (uint64_t i = 0; i < RodataGVs.size(); ++i) {
    SharesForRodata[i] = SharesForRodata[i] * NumGrbgInRodata / SumSharesForRodata;
  }
  for (uint64_t i = 0; i < DataGVs.size(); ++i) {
    SharesForData[i] = SharesForData[i] * NumGrbgInData / SumSharesForData;
  }
  for (uint64_t i = 0; i < BssGVs.size(); ++i) {
    SharesForBss[i] = SharesForBss[i] * NumGrbgInBss / SumSharesForBss;
  }

  // Lastly, insert garbage objects before each global
  for (uint64_t i = 0; i < RodataGVs.size(); ++i) {
    insertGarbageObjects(*RodataGVs[i], SharesForRodata[i]);
  }
  for (uint64_t i = 0; i < DataGVs.size(); ++i) {
    insertGarbageObjects(*DataGVs[i], SharesForData[i]);
  }
  for (uint64_t i = 0; i < BssGVs.size(); ++i) {
    insertGarbageObjects(*BssGVs[i], SharesForBss[i]);
  }

  // Create global guard function
  if (EnableRandezvousGlobalGuard) {
    createGlobalGuardFunction(M);
  }

  // Add all the garbage objects to @llvm.used
  appendToUsed(M, GarbageObjects);

  return true;
}

ModulePass *
llvm::createARMRandezvousGDLR(void) {
  return new ARMRandezvousGDLR();
}
