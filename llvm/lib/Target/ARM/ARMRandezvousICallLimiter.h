//===- ARMRandezvousICallLimiter.h - ARM Randezvous Indirect Call Limiter -===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
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
