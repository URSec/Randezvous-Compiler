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
#include "llvm/IR/Module.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

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
//   GV                - A reference to a GlobalVariable before which to insert
//                       garbage objects.
//   NumGarbageObjects - The number of pointer-sized garbage objects to insert.
//
void
ARMRandezvousGDLR::insertGarbageObjects(GlobalVariable & GV,
                                        uint64_t NumGarbageObjects) {
  Module & M = *GV.getParent();

  //
  // Instead of creating N pointer-sized garbage objects, we create a single
  // garbage array object of N pointer-sized elements
  //

  // Create types for the garbage object
  uint64_t PtrSize = M.getDataLayout().getPointerSize();
  LLVMContext & Ctx = M.getContext();
  PointerType * BlockAddrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
  ArrayType * GarbageObjectTy = ArrayType::get(BlockAddrTy, NumGarbageObjects);

  // Create an initializer for the garbage object
  Constant * Initializer = nullptr;
  if (GV.hasInitializer() && GV.getInitializer()->isZeroValue()) {
    // GV is in BSS, so initialize the garbage object with zeros
    Initializer = Constant::getNullValue(GarbageObjectTy);
  } else if (EnableRandezvousGRBG && !TrapBlocks.empty()) {
    // Initialize the garbage object with addresses of random trap blocks
    SmallVector<Constant *, 0> InitArray;
    for (uint64_t i = 0; i < NumGarbageObjects; ++i) {
      uint64_t Idx = (*RNG)() % TrapBlocks.size();
      const BasicBlock * BB = TrapBlocks[Idx]->getBasicBlock();
      InitArray.push_back(BlockAddress::get(const_cast<BasicBlock *>(BB)));
    }
    Initializer = ConstantArray::get(GarbageObjectTy, InitArray);
  } else {
    // Initialize the garbage object with random values that have the LSB
    // set; this serves as the Thumb bit
    SmallVector<Constant *, 0> InitArray;
    for (uint64_t i = 0; i < NumGarbageObjects; ++i) {
      InitArray.push_back(Constant::getIntegerValue(BlockAddrTy,
                                                    APInt(8 * PtrSize,
                                                          (*RNG)() | 0x1)));
    }
    Initializer = ConstantArray::get(GarbageObjectTy, InitArray);
  }

  // Create the garbage object and insert it before GV
  GlobalVariable * GarbageObject = new GlobalVariable(
    M, GarbageObjectTy, GV.isConstant(), GlobalVariable::InternalLinkage,
    Initializer, GarbageObjectNamePrefix, &GV
  );
  GarbageObject->setAlignment(MaybeAlign(PtrSize));

  // Keep track of the garbage object
  GarbageObjects.push_back(GarbageObject);

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
  } else if (!TrapBlocks.empty()) {
    errs() << "[GDLR] All trap blocks etched!\n";
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
  if (!EnableRandezvousGDLR) {
    return false;
  }

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

  // Add all the garbage objects to @llvm.used
  appendToUsed(M, GarbageObjects);

  return true;
}

ModulePass *
llvm::createARMRandezvousGDLR(void) {
  return new ARMRandezvousGDLR();
}
