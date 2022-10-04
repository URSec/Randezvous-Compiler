//===- ARMRandezvousLGPromote.cpp - ARM Randezvous Local-to-Global Promotion==//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that promotes certain local
// variables that hold function pointers to global variables.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-lgp"

#include "ARMRandezvousInstrumentor.h"
#include "ARMRandezvousLGPromote.h"
#include "ARMRandezvousOptions.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

STATISTIC(NumAllocasPromoted, "Number of allocas promoted to globals");
STATISTIC(NumAllocasSCC, "Number of allocas not promoted due to SCC");
STATISTIC(NumAllocasVarSize, "Number of allocas not promoted due to variable size");

STATISTIC(NumBytesPromoted, "Total size of allocas promoted to globals");

char ARMRandezvousLGPromote::ID = 0;

ARMRandezvousLGPromote::ARMRandezvousLGPromote() : ModulePass(ID) {
}

StringRef
ARMRandezvousLGPromote::getPassName() const {
  return "ARM Randezvous Local-to-Global Promotion Pass";
}

void
ARMRandezvousLGPromote::getAnalysisUsage(AnalysisUsage & AU) const {
  // We need this to access the call graph
  AU.addRequired<CallGraphWrapperPass>();

  AU.setPreservesCFG();
  ModulePass::getAnalysisUsage(AU);
}

//
// Method: runOnModule()
//
// Description:
//   This method is called when the PassManager wants this pass to transform
//   the specified Module.  This method promotes all static local variables in
//   a non-recursive function that contain one or more function pointers into
//   global variables.
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
ARMRandezvousLGPromote::runOnModule(Module & M) {
  if (!EnableRandezvousLGPromote) {
    return false;
  }

  bool changed = false;
  const DataLayout & DL = M.getDataLayout();

  // Loop over SCCs instead of functions; this allows us to naturally skip
  // recursive functions
  CallGraph & CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  for (scc_iterator<CallGraph *> SCC = scc_begin(&CG); !SCC.isAtEnd(); ++SCC) {
    // Skip recursive functions but collect statistics from them
    if (SCC.hasCycle()) {
      for (CallGraphNode * Node : *SCC) {
        if (Function * F = Node->getFunction()) {
          for (BasicBlock & BB : *F) {
            for (Instruction & I : BB) {
              if (AllocaInst * AI = dyn_cast<AllocaInst>(&I)) {
                if (containsFunctionPointerType(AI->getAllocatedType())) {
                  ++NumAllocasSCC;
                }
              }
            }
          }
        }
      }
      continue;
    }

    Function * F = SCC->front()->getFunction();
    if (F == nullptr) {
      continue;
    }

    // Identify alloca instructions in the function
    std::vector<AllocaInst *> Allocas;
    for (BasicBlock & BB : *F) {
      for (Instruction & I : BB) {
        if (AllocaInst * AI = dyn_cast<AllocaInst>(&I)) {
          Allocas.push_back(AI);
        }
      }
    }

    // Promote static allocas that contain function pointers to globals
    for (AllocaInst * AI : Allocas) {
      Type * AllocatedTy = AI->getAllocatedType();
      if (containsFunctionPointerType(AllocatedTy)) {
        if (!AI->isStaticAlloca()) {
          ++NumAllocasVarSize;
          continue;
        }

        GlobalVariable * GV = new GlobalVariable(
          M, AllocatedTy, false, GlobalVariable::InternalLinkage,
          createNonZeroInitializerFor(AllocatedTy),
          F->getName() + "." + AI->getName()
        );
        GV->setAlignment(AI->getAlign());

        AI->replaceAllUsesWith(GV);
        AI->eraseFromParent();
        ++NumAllocasPromoted;
        NumBytesPromoted += DL.getTypeAllocSize(AllocatedTy);
        changed = true;
      }
    }
  }

  return changed;
}

ModulePass *
llvm::createARMRandezvousLGPromote(void) {
  return new ARMRandezvousLGPromote();
}
