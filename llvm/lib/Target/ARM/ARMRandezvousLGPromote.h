//===- ARMRandezvousLGPromote.h - ARM Randezvous Local-to-Global Promotion ===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that promotes certain local
// variables that hold function pointers to global variables.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_LGP
#define ARM_RANDEZVOUS_LGP

#include "llvm/Pass.h"

namespace llvm {
  struct ARMRandezvousLGPromote : public ModulePass {
    // Pass Identifier
    static char ID;

    ARMRandezvousLGPromote();
    virtual StringRef getPassName() const override;
    void getAnalysisUsage(AnalysisUsage & AU) const override;
    virtual bool runOnModule(Module & M) override;
  };

  ModulePass * createARMRandezvousLGPromote(void);
}

#endif
