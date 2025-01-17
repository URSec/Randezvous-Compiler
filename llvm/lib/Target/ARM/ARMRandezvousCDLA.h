//===- ARMRandezvousCDLA.h - ARM Randezvous Control Data Leakage Analysis -===//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that analyzes control data
// leakage of ARM machine code.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_CDLA
#define ARM_RANDEZVOUS_CDLA

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Pass.h"

#include <set>

namespace llvm {
  struct ARMRandezvousCDLA : public ModulePass {
    // Pass Identifier
    static char ID;

    ARMRandezvousCDLA(bool Xformed);
    virtual StringRef getPassName() const override;
    void getAnalysisUsage(AnalysisUsage & AU) const override;
    void releaseMemory() override;
    virtual bool runOnModule(Module & M) override;

  private:
    // Whether we are analyzing transformed code
    bool Xformed = false;

    std::set<const MachineBasicBlock *> LeakableMBBs;

    bool determineLeakability(const MachineBasicBlock & MBB);
    bool canSpillLinkRegister(const Function & F);
  };

  ModulePass * createARMRandezvousCDLA(bool Xformed);
}

#endif
