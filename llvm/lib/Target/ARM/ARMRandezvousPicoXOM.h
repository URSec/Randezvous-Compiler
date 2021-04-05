//===- ARMRandezvousPicoXOM.h - ARM Randezvous Execute-Only Memory --------===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that forces functions to be
// generated as execute-only code.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_PICOXOM
#define ARM_RANDEZVOUS_PICOXOM

#include "llvm/Pass.h"

namespace llvm {
  struct ARMRandezvousPicoXOM : public ModulePass {
    // Pass Identifier
    static char ID;

    ARMRandezvousPicoXOM();
    virtual StringRef getPassName() const override;
    virtual bool runOnModule(Module & M) override;
  };

  ModulePass * createARMRandezvousPicoXOM(void);
}

#endif
