//===- ARMRandezvousPicoXOM.cpp - ARM Randezvous Execute-Only Memory ------===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that forces functions to be
// generated as execute-only code.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-picoxom"

#include "ARMRandezvousOptions.h"
#include "ARMRandezvousPicoXOM.h"
#include "llvm/IR/Module.h"

using namespace llvm;

char ARMRandezvousPicoXOM::ID = 0;

ARMRandezvousPicoXOM::ARMRandezvousPicoXOM() : ModulePass(ID) {
}

StringRef
ARMRandezvousPicoXOM::getPassName() const {
  return "ARM Randezvous Execute-Only Memory Pass";
}

//
// Method: runOnModule()
//
// Description:
//   This method is called when the PassManager wants this pass to transform
//   the specified Module.  This method adds a "+execute-only" feature string
//   to the "target-features" attribute of each function in the module, so that
//   the generated code of each function will not read data from the code
//   segment.
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
ARMRandezvousPicoXOM::runOnModule(Module & M) {
  if (!EnableRandezvousPicoXOM) {
    return false;
  }

  static constexpr StringRef TargetFeatures = "target-features";
  static constexpr StringRef FSExecOnly = "+execute-only";

  bool changed = false;
  for (Function & F : M) {
    Attribute FSAttr = F.getFnAttribute(TargetFeatures);
    StringRef FS = FSAttr.hasAttribute(Attribute::None) ?
                   "" : FSAttr.getValueAsString();
    if (FS.contains(FSExecOnly)) {
      // Nothing to be done
      continue;
    }

    // Add the execute-only feature
    std::string NewFS = FS.empty() ? FSExecOnly.str() :
                                     FS.str() + "," + FSExecOnly.str();

    F.addFnAttr(TargetFeatures, NewFS);
    changed = true;
  }

  return changed;
}

ModulePass *
llvm::createARMRandezvousPicoXOM(void) {
  return new ARMRandezvousPicoXOM();
}
