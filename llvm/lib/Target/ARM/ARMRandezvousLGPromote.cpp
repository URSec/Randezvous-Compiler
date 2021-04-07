//===- ARMRandezvousLGPromote.cpp - ARM Randezvous Local-to-Global Promotion==//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that promotes certain local
// variables that hold function pointers to global variables.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-lgp"

#include "ARMRandezvousLGPromote.h"
#include "ARMRandezvousOptions.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

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
// Function: containsFunctionPointerType()
//
// Description:
//   This function examines a Type to see whether it can explicitly contain one
//   or more function pointers.  Note that this function recurses on aggregate
//   types.
//
// Input:
//   Ty - A pointer to a Type to examine.
//
// Return value:
//   true  - The Type can contain one or more function pointers.
//   false - The Type does not contain a function pointer.
//
static bool
containsFunctionPointerType(Type * Ty) {
  // Pointer
  if (PointerType * PtrTy = dyn_cast<PointerType>(Ty)) {
    return PtrTy->getElementType()->isFunctionTy();
  }

  // Array
  if (ArrayType * ArrayTy = dyn_cast<ArrayType>(Ty)) {
    return containsFunctionPointerType(ArrayTy->getElementType());
  }

  // Struct
  if (StructType * StructTy = dyn_cast<StructType>(Ty)) {
    for (Type * ElementTy : StructTy->elements()) {
      if (containsFunctionPointerType(ElementTy)) {
        return true;
      }
    }
  }

  // Other types do not contain function pointers
  return false;
}

//
// Function: createNonZeroInitializerFor()
//
// Description:
//   This function creates a non-zero Constant initializer for a give Type,
//   which is supposed to contain one or more function pointers.  Note that
//   this function recurses on aggregate types.
//
// Input:
//   Ty - A pointer to a Type for which to create an initializer.
//
// Return value:
//   A pointer to a created Constant.
//
static Constant *
createNonZeroInitializerFor(Type * Ty) {
  // Pointer: this is where we insert non-zero values
  if (PointerType * PtrTy = dyn_cast<PointerType>(Ty)) {
    return ConstantExpr::getIntToPtr(
      ConstantInt::get(Type::getInt32Ty(Ty->getContext()), 1), Ty
    );
  }

  // Array
  if (ArrayType * ArrayTy = dyn_cast<ArrayType>(Ty)) {
    std::vector<Constant *> InitArray;
    for (uint64_t i = 0; i < ArrayTy->getNumElements(); ++i) {
      InitArray.push_back(createNonZeroInitializerFor(ArrayTy->getElementType()));
    }
    return ConstantArray::get(ArrayTy, InitArray);
  }

  // Struct
  if (StructType * StructTy = dyn_cast<StructType>(Ty)) {
    std::vector<Constant *> InitArray;
    for (unsigned i = 0; i < StructTy->getNumElements(); ++i) {
      InitArray.push_back(createNonZeroInitializerFor(StructTy->getElementType(i)));
    }
    return ConstantStruct::get(StructTy, InitArray);
  }

  // Zeroing out other types are fine
  return Constant::getNullValue(Ty);
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

  // Loop over SCCs instead of functions; this allows us to naturally skip
  // recursive functions
  CallGraph & CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  for (scc_iterator<CallGraph *> SCC = scc_begin(&CG); !SCC.isAtEnd(); ++SCC) {
    // Skip recursive functions
    if (SCC.hasCycle()) {
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
      if (AI->isStaticAlloca() && containsFunctionPointerType(AllocatedTy)) {
        GlobalVariable * GV = new GlobalVariable(
          M, AllocatedTy, false, GlobalVariable::InternalLinkage,
          createNonZeroInitializerFor(AllocatedTy),
          F->getName() + "." + AI->getName()
        );
        GV->setAlignment(AI->getAlign());

        AI->replaceAllUsesWith(GV);
        AI->eraseFromParent();
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
