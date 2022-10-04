//===- ARMRandezvousLGPromote.h - ARM Randezvous Local-to-Global Promotion ===//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
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
