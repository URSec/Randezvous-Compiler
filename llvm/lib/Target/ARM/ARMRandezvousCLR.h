//===- ARMRandezvousCLR.h - ARM Randezvous Code Layout Randomization ------===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that randomizes the code layout
// of ARM machine code.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_CLR
#define ARM_RANDEZVOUS_CLR

#include "ARMRandezvousInstrumentor.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"

namespace llvm {
  struct ARMRandezvousCLR : public ModulePass, ARMRandezvousInstrumentor {
    // Pass Identifier
    static char ID;

    ARMRandezvousCLR();
    virtual StringRef getPassName() const override;
    void getAnalysisUsage(AnalysisUsage & AU) const override;
    virtual bool runOnModule(Module & M) override;

  private:
    std::unique_ptr<RandomNumberGenerator> RNG;

    void shuffleMachineBasicBlocks(MachineFunction & MF);
    void insertTrapBlocks(Function & F, MachineFunction & MF,
                          uint64_t NumTrapInsts);
  };

  ModulePass * createARMRandezvousCLR(void);
}

#endif
