//===- ARMRandezvousLGPromote.h - ARM Local to Global Promotion ------===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a pass that promotes local variables
// that hold function pointers to global variables
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_LGP
#define ARM_RANDEZVOUS_LGP

#include "ARMRandezvousInstrumentor.h"
#include "llvm/Pass.h"

namespace llvm {
  struct ARMRandezvousLGPromote : public ModulePass, ARMRandezvousInstrumentor {
    // Pass Identifier
    static char ID;

    ARMRandezvousLGPromote();
    virtual StringRef getPassName() const override;
    virtual bool runOnModule(Module & M) override;

  private:
    bool isFunctionPointerType(Type *type);
    Type *getTypeForGlobal(Type *type);

  };

  ModulePass * createARMRandezvousLGPromote(void);
}

#endif
