//===- ARMRandezvousICallLimiter.h - ARM Randezvous Indirect Call Limiter -===//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that limits the physical register
// to be used in indirect call instructions in ARM machine code.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_ICALL_LIMITER
#define ARM_RANDEZVOUS_ICALL_LIMITER

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {
  struct ARMRandezvousICallLimiter : public MachineFunctionPass {
    // Pass Identifier
    static char ID;

    ARMRandezvousICallLimiter();
    virtual StringRef getPassName() const override;
    virtual bool runOnMachineFunction(MachineFunction & MF) override;
  };

  FunctionPass * createARMRandezvousICallLimiter(void);
}

#endif
