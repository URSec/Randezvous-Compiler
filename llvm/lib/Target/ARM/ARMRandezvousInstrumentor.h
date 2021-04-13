//===- ARMRandezvousInstrumentor.h - A helper class for instrumentation ---===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces of a class that can help passes of its
// subclass easily instrument ARM machine IR without concerns of breaking IT
// blocks.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_INSTRUMENTOR
#define ARM_RANDEZVOUS_INSTRUMENTOR

#include "ARMBaseInstrInfo.h"

namespace llvm {
  //====================================================================
  // Static inline functions.
  //====================================================================

  static inline size_t getBasicBlockCodeSize(const MachineBasicBlock & MBB) {
    const MachineFunction & MF = *MBB.getParent();
    const TargetInstrInfo * TII = MF.getSubtarget().getInstrInfo();

    size_t CodeSize = 0ul;
    for (const MachineInstr & MI : MBB) {
      CodeSize += TII->getInstSizeInBytes(MI);
    }

    return CodeSize;
  }

  //
  // Function: getFunctionCodeSize()
  //
  // Description:
  //   This function computes the code size of a machine function.
  //
  // Input:
  //   MF - A reference to the target machine function.
  //
  // Return value:
  //   The size (in bytes) of the machine function.
  //
  static inline size_t getFunctionCodeSize(const MachineFunction & MF) {
    size_t CodeSize = 0ul;
    for (const MachineBasicBlock & MBB : MF) {
      CodeSize += getBasicBlockCodeSize(MBB);
    }

    return CodeSize;
  }

  //====================================================================
  // Class ARMRandezvousInstrumentor.
  //====================================================================

  struct ARMRandezvousInstrumentor {
    void insertInstBefore(MachineInstr & MI, MachineInstr * Inst);

    void insertInstAfter(MachineInstr & MI, MachineInstr * Inst);

    void insertInstsBefore(MachineInstr & MI, ArrayRef<MachineInstr *> Insts);

    void insertInstsAfter(MachineInstr & MI, ArrayRef<MachineInstr *> Insts);

    void removeInst(MachineInstr & MI);

    MachineBasicBlock * splitBasicBlockBefore(MachineInstr & MI);

    MachineBasicBlock * splitBasicBlockAfter(MachineInstr & MI);

    std::vector<Register> findFreeRegistersBefore(const MachineInstr & MI,
                                                  bool Thumb = false);
    std::vector<Register> findFreeRegistersAfter(const MachineInstr & MI,
                                                 bool Thumb = false);

  private:
    unsigned getITBlockSize(const MachineInstr & IT);
    MachineInstr * findIT(MachineInstr & MI, unsigned & distance);
    const MachineInstr * findIT(const MachineInstr & MI, unsigned & distance);
    std::deque<bool> decodeITMask(unsigned Mask);
    unsigned encodeITMask(std::deque<bool> DQMask);
  };
}

#endif
