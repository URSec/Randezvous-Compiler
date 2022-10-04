//===- ARMRandezvousPicoXOM.h - ARM Randezvous Execute-Only Memory --------===//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
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
