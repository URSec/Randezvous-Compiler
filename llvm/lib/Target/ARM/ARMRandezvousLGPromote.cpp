//===- ARMRandezvousCLR.cpp - ARM Randezvous Code Layout Randomization ----===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that randomizes the code
// layout of ARM machine code.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-lgp"

#include "ARMRandezvousLGPromote.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/CodeMetrics.h"

using namespace llvm;

static cl::opt<bool>
EnableRandezvousLGPromote("arm-randezvous-lgp",
                    cl::Hidden,
                    cl::desc("Enable ARM Randezvous Local-to-Global Function Pointer Promotion"),
                    cl::init(false));

char ARMRandezvousLGPromote::ID = 0;

ARMRandezvousLGPromote::ARMRandezvousLGPromote() : ModulePass(ID) {
}

StringRef
ARMRandezvousLGPromote::getPassName() const {
  return "ARM Randezvous Local-to-Global Function Pointer Promotion Pass";
}

bool
ARMRandezvousLGPromote::isFunctionPointerType(Type *type) {
  if (PointerType *pt=dyn_cast<PointerType>(type)) {
    if (PointerType *pt2=dyn_cast<PointerType>(pt->getElementType())) {
      if (pt2->getElementType()->isFunctionTy()) {
        errs() << "[LGP] FunctionType found " << *(pt2->getElementType()) << "\n";
        return true;
        //return isFunctionPointerType(pointerType->getElementType());
      }
    }
  }

  return false;

  // Check the type here
  if (PointerType *pointerType=dyn_cast<PointerType>(type)) {
    return isFunctionPointerType(pointerType->getElementType());
  }
  // Exit Condition
  else if (type->isFunctionTy()) {
    return  true;
  }
  return false;
}

Type * 
ARMRandezvousLGPromote::getTypeForGlobal(Type *type) {

  if (PointerType *pt=dyn_cast<PointerType>(type))
    return pt->getElementType();

  errs() << "[LGP] Shouldn't reach here\n";
  return NULL;
}

//
// Method: runOnModule()
//
// Description:
//   This method is called when the PassManager wants this pass to transform
//   the specified Module.  This method shuffles the order of functions within
//   the module and/or the order of basic blocks within each function, and
//   inserts trap instructions to fill the text section.
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
  errs() << "[LGP] Running Randezvous Local-to-Global Function Pointer Pass\n";
  if (!EnableRandezvousLGPromote) {
    errs() << "[LGP] Abort. Pass not enabled.\n";
    return false;
  }

  // Identify recursive functions. Recursive function pointers will not be
  // promoted to global variables since we only reserve a single location
  // in the global region to store the function pointer, and at runtime, a
  // recursive function pointers may need to dynamically point to distinct
  // functions depending on the current frame.
  std::set<const Function *> recursive_funs;
  for (Function &F : M)
    for (BasicBlock &BB: F)
      for (Instruction &I : BB) 
        if (const auto *Call = dyn_cast<CallBase>(&I)) {
          if (const Function *F = Call->getCalledFunction()) {
            // lParent function is the same as callee
            if (F == BB.getParent())
              recursive_funs.insert(F);
            // TODO: Add case for non-obvious recursion, i.e. when the
            // parent recursive function's frame is separated by another
            // distinct function on the stack (e.g. Parent's parent is the
            // same as callee).
          }
        }
  
  // Identify allocat instructions
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        // Locate alloca instructions
        if (AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
          // Check if the alloca is allocating a function pointer
          if (isFunctionPointerType(AI->getType())) {
            // TODO: Add code to check for and skip recursive functions
            // Promoting this function pointer alloca to a global
            errs() << "[LGP] function pointer alloca: " << I << "\n";
            // Get global parameters
            //StringRef glob_name = ("LGP_" + F.getName() + "_" + AI->getName()).str();
            StringRef glob_name = AI->getName();
            // TODO replace with AI->getAllocatedType
            Type* glob_type = getTypeForGlobal(AI->getType());
            // Get reference to global object
            Constant *new_global = M.getOrInsertGlobal(glob_name, glob_type);
            // TODO change global variable fetcher to omit the name
            if (GlobalVariable *gv=dyn_cast<GlobalVariable>(new_global)) {
              gv->setLinkage(GlobalValue::PrivateLinkage);
              gv->setInitializer(Constant::getNullValue(gv->getValueType()));
            }
            // Replace local uses of function pointer with global
            AI->replaceAllUsesWith(new_global);
          }
        }
      }
    }
  }
  return true;
}

ModulePass *
llvm::createARMRandezvousLGPromote(void) {
  return new ARMRandezvousLGPromote();
}
