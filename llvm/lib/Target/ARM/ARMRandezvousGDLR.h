//===- ARMRandezvousGDLR.h - ARM Randezvous Global Data Layout Randomization =//
//
// Copyright (c) 2021-2022, University of Rochester
//
// Part of the Randezvous Project, under the Apache License v2.0 with
// LLVM Exceptions.  See LICENSE.txt in the llvm directory for license
// information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that randomizes the layout of
// global data regions for ARM machine code.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_GDLR
#define ARM_RANDEZVOUS_GDLR

#include "ARMRandezvousInstrumentor.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"

namespace llvm {
  struct ARMRandezvousGDLR : public ModulePass, ARMRandezvousInstrumentor {
    // Pass Identifier
    static char ID;

    static constexpr StringRef GarbageObjectNamePrefix = "__randezvous_garbage";
    static constexpr StringRef GlobalGuardFuncName = "__randezvous_globalguard_getaddr";

    ARMRandezvousGDLR();
    virtual StringRef getPassName() const override;
    void getAnalysisUsage(AnalysisUsage & AU) const override;
    virtual void releaseMemory() override;
    virtual bool runOnModule(Module & M) override;

  private:
    std::unique_ptr<RandomNumberGenerator> RNG;
    std::vector<MachineBasicBlock *> TrapBlocks;
    std::vector<MachineBasicBlock *> TrapBlocksUnetched;
    std::vector<MachineBasicBlock *> TrapBlocksEtched;
    std::vector<GlobalValue *> GarbageObjects;
    std::vector<GlobalValue *> GarbageObjectsEligibleForGlobalGuard;

    Function * createGlobalGuardFunction(Module & M);
    void insertGarbageObjects(GlobalVariable & GV, uint64_t NumGarbages);
  };

  ModulePass * createARMRandezvousGDLR(void);
}

#endif
