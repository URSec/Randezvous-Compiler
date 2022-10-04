//===- ARMRandezvousCDLA.cpp - ARM Randezvous Control Data Leakage Analysis ==//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that analyzes control data
// leakage of ARM machine code.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-cdla"

#include "ARMRandezvousCDLA.h"
#include "ARMRandezvousInstrumentor.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

using namespace llvm;

// Statistics collected before Randezvous transformation passes
STATISTIC(OriginalCodeSize,
          "Size of the original code in bytes");
STATISTIC(OriginalCodeSizeLeakable,
          "Size of the original code leakable in bytes");
STATISTIC(OriginalCodeSizeLeakableViaFuncPtr,
          "Size of the original code leakable via function pointers in bytes");
STATISTIC(OriginalCodeSizeLeakableViaRetAddr,
          "Size of the original code leakable via return addresses in bytes");
STATISTIC(OriginalNumFuncs,
          "Number of functions in the original code");
STATISTIC(OriginalNumFuncsLeakable,
          "Number of leakable functions in the original code");
STATISTIC(OriginalNumBBs,
          "Number of basic blocks in the original code");
STATISTIC(OriginalNumBBsLeakable,
          "Number of leakable basic blocks in the original code");

// Statistics collected after Randezvous transformation passes
STATISTIC(XformedCodeSize,
          "Size of the transformed code in bytes");
STATISTIC(XformedCodeSizeLeakable,
          "Size of the transformed code leakable in bytes");
STATISTIC(XformedCodeSizeLeakableViaFuncPtr,
          "Size of the transformed code leakable via function pointers in bytes");
STATISTIC(XformedCodeSizeLeakableViaRetAddr,
          "Size of the transformed code leakable via return addresses in bytes");
STATISTIC(XformedNumFuncs,
          "Number of functions in the transformed code");
STATISTIC(XformedNumFuncsLeakable,
          "Number of leakable functions in the transformed code");
STATISTIC(XformedNumBBs,
          "Number of basic blocks in the transformed code");
STATISTIC(XformedNumBBsLeakable,
          "Number of leakable basic blocks in the transformed code");

char ARMRandezvousCDLA::ID = 0;

ARMRandezvousCDLA::ARMRandezvousCDLA(bool Xformed)
    : ModulePass(ID), Xformed(Xformed) {
}

StringRef
ARMRandezvousCDLA::getPassName() const {
  return "ARM Randezvous Control Data Leakage Analysis Pass";
}

void
ARMRandezvousCDLA::getAnalysisUsage(AnalysisUsage & AU) const {
  // We need this to access MachineFunctions
  AU.addRequired<MachineModuleInfoWrapperPass>();

  AU.setPreservesAll();
  ModulePass::getAnalysisUsage(AU);
}

void
ARMRandezvousCDLA::releaseMemory() {
  LeakableMBBs.clear();
}

//
// Function: isReallyAddressTaken()
//
// Description:
//   This function examines the use chain of a specified Function to see if
//   its address really escapes to memory or if the Function is considered to
//   be address-taken because it is used in some way (e.g., in a compare
//   instruction, a global alias, or a select instruction).
//
// Input:
//   F - A const reference to the Function.
//
// Return value:
//   true  - There may be a use of the Function that stores its address to
//           memory.
//   false - There is no use of the Function that may store its address to
//           memory.
//
static bool
isReallyAddressTaken(const Function & F) {
  if (!F.hasAddressTaken()) {
    return false;
  }

  std::vector<const Value *> Worklist;
  std::set<const Value *> PHIs;

  Worklist.push_back(&F);
  while (!Worklist.empty()) {
    const Value * V = Worklist.back();
    Worklist.pop_back();

    //
    // Examine all uses of the value.
    //
    for (const User * U : V->users()) {
      if (isa<PHINode>(U)) {
        // Follow each PHINode once
        if (PHIs.count(U) == 0) {
          PHIs.insert(U);
          Worklist.push_back(U);
        }
      } else if (isa<GlobalAlias>(U)) {
        // Follow aliases
        Worklist.push_back(U);
      } else if (const BlockAddress * BA = dyn_cast<BlockAddress>(U)) {
        // Block addresses are fine
        continue;
      } else if (const ConstantExpr * CE = dyn_cast<ConstantExpr>(U)) {
        // Follow constant expressions except compares
        if (CE->isCompare()) {
          continue;
        }
        Worklist.push_back(U);
      } else if (const GlobalVariable * GV = dyn_cast<GlobalVariable>(U)) {
        // Globals are stored in memory except certain LLVM metadata
        if (GV->getName() != "llvm.used" &&
            GV->getName() != "llvm.compiler.used") {
          return true;
        }
      } else if (isa<Constant>(U)) {
        // Follow all other constants
        Worklist.push_back(U);
      } else if (isa<StoreInst>(U)) {
        // Stores write to memory
        return true;
      } else if (isa<CastInst>(U)) {
        // Follow casts
        Worklist.push_back(U);
      } else if (isa<SelectInst>(U)) {
        // Follow selects
        Worklist.push_back(U);
      } else if (isa<CmpInst>(U)) {
        // Compares are fine
        continue;
      } else if (const CallInst * CI = dyn_cast<CallInst>(U)) {
        // Function call arguments cannot be analyzed
        if (CI->hasArgument(V)) {
          return true;
        }
      } else {
        errs() << "[CDLA] Unrecognized use of @" << F.getName() << ": "
               << *U << "\n";
      }
    }
  }

  return false;
}

//
// Method: canSpillLinkRegister()
//
// Description:
//   This method checks if a specified Function can spill the return address in
//   Link Register (LR) to memory.  Note that this method recurses in cases
//   where it needs to follow tail calls.
//
// Input:
//   F - A const reference to the Function.
//
// Return value:
//   true  - The Function may spill LR to memory.
//   false - The Function does not spill LR to memory.
//
bool
ARMRandezvousCDLA::canSpillLinkRegister(const Function & F) {
  MachineModuleInfo & MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  const MachineFunction * MF = MMI.getMachineFunction(F);
  if (MF == nullptr) {
    // External functions do not have MachineFunction available, so return true
    // conservatively
    return true;
  }

  const MachineFrameInfo & MFI = MF->getFrameInfo();
  if (!MFI.isCalleeSavedInfoValid()) {
    // CalleeSavedInfo not valid, return true conservatively
    return true;
  }

  for (const CalleeSavedInfo & CSI : MFI.getCalleeSavedInfo()) {
    if (CSI.getReg() == ARM::LR) {
      // Most functions end up here
      return true;
    }
  }

  // Even if the function (F) itself does not spill LR, it might tail-call
  // another function that does spill LR, in which case LR still points to F's
  // caller and therefore we should return true
  for (const MachineBasicBlock & MBB : *MF) {
    for (const MachineInstr & MI : MBB) {
      switch (MI.getOpcode()) {
      case ARM::tTAILJMPd:
      case ARM::tTAILJMPdND: {
        const MachineOperand & MO = MI.getOperand(0);
        if (MO.isGlobal()) {
          // Go through aliases and ifuncs
          const GlobalValue * GV = MO.getGlobal();
          while (!isa<Function>(GV)) {
            if (const auto * GIS = dyn_cast<GlobalIndirectSymbol>(GV)) {
              GV = GIS->getBaseObject();
            } else {
              llvm_unreachable("Invalid type of global!");
            }
          }
          const Function * F2 = dyn_cast<Function>(GV);
          if (canSpillLinkRegister(*F2)) {
            return true;
          }
        } else if (MO.isSymbol()) {
          // Don't know the callee, so return true conservatively
          return true;
        } else {
          llvm_unreachable("Unrecognized type of MachineOperand!");
        }
        break;
      }

      case ARM::tTAILJMPr:
        // Don't know the callee, so return true conservatively
        return true;

      default:
        break;
      }
    }
  }

  return false;
}

//
// Method: determineLeakability()
//
// Description:
//   This method determines if the address of a specified MachineBasicBlock can
//   be leaked.  A MachineBasicBlock is considered leakable if
//
//   * its layout predecessor is leakable, or
//
//   * the last instruction of its layout predecessor is a call to a function
//     that might spill LR to memory, or
//
//   * it contains a call to a function that might spill LR to memory and the
//     call is not the last instruction.
//
// Input:
//   MBB - A const reference to the MachineBasicBlock.
//
// Return value:
//   true  - The address of the MachineBasicBlock may be leakable.
//   false - The address of the MachineBasicBlock is not leakable.
//
bool
ARMRandezvousCDLA::determineLeakability(const MachineBasicBlock & MBB) {
  // First examine MBB's layout predecessor
  MachineFunction::const_reverse_iterator LayoutPred(MBB);
  LayoutPred = std::next(LayoutPred);
  if (LayoutPred != MBB.getParent()->rend()) {
    // If the layout predecessor is leakable, MBB becomes leakable
    if (LeakableMBBs.count(&*LayoutPred) != 0) {
      return true;
    }
    // If the layout predecessor ends up with a call to a function that might
    // spill LR, then MBB is also leakable
    MachineBasicBlock::const_iterator MI = LayoutPred->getLastNonDebugInstr();
    if (MI != LayoutPred->end()) {
      switch (MI->getOpcode()) {
      case ARM::tBL:
      case ARM::tBLXi: {
        const MachineOperand & MO = MI->getOperand(2);
        if (MO.isGlobal()) {
          // Go through aliases and ifuncs
          const GlobalValue * GV = MO.getGlobal();
          while (!isa<Function>(GV)) {
            if (const auto * GIS = dyn_cast<GlobalIndirectSymbol>(GV)) {
              GV = GIS->getBaseObject();
            } else {
              llvm_unreachable("Invalid type of global!");
            }
          }
          const Function * F = dyn_cast<Function>(GV);
          if (canSpillLinkRegister(*F)) {
            return true;
          }
        } else if (MO.isSymbol()) {
          // Don't know the callee, so return true conservatively
          return true;
        } else {
          llvm_unreachable("Unrecognized type of MachineOperand!");
        }
        break;
      }

      case ARM::tBLXr:
      case ARM::tBLXr_Randezvous:
        // Don't know the callee, so return true conservatively
        return true;

      default:
        break;
      }
    }
  }

  // Now examine MBB's own instructions: a non-last call instruction to a
  // function that might spill LR makes MBB leakable
  for (const MachineInstr & MI : MBB) {
    switch (MI.getOpcode()) {
    case ARM::tBL:
    case ARM::tBLXi: {
      const MachineOperand & MO = MI.getOperand(2);
      if (MO.isGlobal()) {
        // Go through aliases and ifuncs
        const GlobalValue * GV = MO.getGlobal();
        while (!isa<Function>(GV)) {
          if (const auto * GIS = dyn_cast<GlobalIndirectSymbol>(GV)) {
            GV = GIS->getBaseObject();
          } else {
            llvm_unreachable("Invalid type of global!");
          }
        }
        const Function * F = dyn_cast<Function>(GV);
        if (canSpillLinkRegister(*F) && MBB.getLastNonDebugInstr() != &MI) {
          return true;
        }
      } else if (MO.isSymbol()) {
        if (MBB.getLastNonDebugInstr() != &MI) {
          // Don't know the callee, so return true conservatively
          return true;
        }
      } else {
        llvm_unreachable("Unrecognized type of MachineOperand!");
      }
      break;
    }

    case ARM::tBLXr:
    case ARM::tBLXr_Randezvous:
      if (MBB.getLastNonDebugInstr() != &MI) {
        // Don't know the callee, so return true conservatively
        return true;
      }
      break;

    default:
      break;
    }
  }

  // Great!  In this case MBB is not leakable via function calls
  return false;
}

//
// Method: runOnModule()
//
// Description:
//   This method is called when the PassManager wants this pass to transform
//   the specified Module.  This method analyzes the Module and collects
//   statistics about control data leakage on the Module.
//
// Input:
//   M - A reference to the Module to analyze.
//
// Return value:
//   false - The Module was not modified.
//
bool
ARMRandezvousCDLA::runOnModule(Module & M) {
  MachineModuleInfo & MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();

  size_t CodeSize = 0;
  size_t CodeSizeLeakable = 0;
  size_t CodeSizeLeakableViaFuncPtr = 0;
  size_t CodeSizeLeakableViaRetAddr = 0;
  uint64_t NumFuncs = 0;
  uint64_t NumFuncsLeakable = 0;
  uint64_t NumBBs = 0;
  uint64_t NumBBsLeakable = 0;
  for (Function & F : M) {
    const MachineFunction * MF = MMI.getMachineFunction(F);
    if (MF == nullptr) {
      continue;
    }

    // Count the number of basic blocks, the number of functions, and code size
    ++NumFuncs;
    for (const MachineBasicBlock & MBB : *MF) {
      if (!MBB.isRandezvousTrapBlock()) {
        ++NumBBs;
        CodeSize += getBasicBlockCodeSize(MBB);
      }
    }

    // Mark the entire function leakable if the function's address escapes to
    // memory
    if (isReallyAddressTaken(F)) {
      ++NumFuncsLeakable;
      for (const MachineBasicBlock & MBB : *MF) {
        if (!MBB.isRandezvousTrapBlock()) {
          size_t MBBCodeSize = getBasicBlockCodeSize(MBB);
          CodeSizeLeakableViaFuncPtr += MBBCodeSize;
          if (LeakableMBBs.count(&MBB) == 0) {
            CodeSizeLeakable += MBBCodeSize;
            LeakableMBBs.insert(&MBB);
          }
        }
      }
    }

    // Analyze individual basic blocks
    for (const MachineBasicBlock & MBB : *MF) {
      if (!MBB.isRandezvousTrapBlock()) {
        if (determineLeakability(MBB)) {
          size_t MBBCodeSize = getBasicBlockCodeSize(MBB);
          ++NumBBsLeakable;
          CodeSizeLeakableViaRetAddr += MBBCodeSize;
          if (LeakableMBBs.count(&MBB) == 0) {
            CodeSizeLeakable += MBBCodeSize;
            LeakableMBBs.insert(&MBB);
          }
        }
      }
    }
  }

  // Update statistics
  if (Xformed) {
    XformedCodeSize += CodeSize;
    XformedCodeSizeLeakable += CodeSizeLeakable;
    XformedCodeSizeLeakableViaFuncPtr += CodeSizeLeakableViaFuncPtr;
    XformedCodeSizeLeakableViaRetAddr += CodeSizeLeakableViaRetAddr;
    XformedNumFuncs += NumFuncs;
    XformedNumFuncsLeakable += NumFuncsLeakable;
    XformedNumBBs += NumBBs;
    XformedNumBBsLeakable += NumBBsLeakable;
  } else {
    OriginalCodeSize += CodeSize;
    OriginalCodeSizeLeakable += CodeSizeLeakable;
    OriginalCodeSizeLeakableViaFuncPtr += CodeSizeLeakableViaFuncPtr;
    OriginalCodeSizeLeakableViaRetAddr += CodeSizeLeakableViaRetAddr;
    OriginalNumFuncs += NumFuncs;
    OriginalNumFuncsLeakable += NumFuncsLeakable;
    OriginalNumBBs += NumBBs;
    OriginalNumBBsLeakable += NumBBsLeakable;
  }

  // We did not make any change
  return false;
}

ModulePass *
llvm::createARMRandezvousCDLA(bool Xformed) {
  return new ARMRandezvousCDLA(Xformed);
}
