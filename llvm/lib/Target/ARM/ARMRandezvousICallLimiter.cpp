//===- ARMRandezvousICallLimiter.cpp - ARM Randezvous Indirect Call Limiter ==//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of a pass that limits the physical
// register to be used in indirect call instructions in ARM machine code.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-randezvous-icall-limiter"

#include "ARMBaseInstrInfo.h"
#include "ARMRandezvousICallLimiter.h"
#include "ARMRandezvousOptions.h"
#include "ARMRegisterInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

STATISTIC(NumICallsLimited, "Number of indirect calls limited");

char ARMRandezvousICallLimiter::ID = 0;

ARMRandezvousICallLimiter::ARMRandezvousICallLimiter() : MachineFunctionPass(ID) {
}

StringRef
ARMRandezvousICallLimiter::getPassName() const {
  return "ARM Randezvous Indirect Call Limiter Pass";
}

//
// Method: runOnMachineFunction()
//
// Description:
//   This method is called when the PassManager wants this pass to transform
//   the specified MachineFunction.  This method limits the register used in
//   all indirect function calls to be a non-callee-saved register, in order to
//   avoid unnecessary potential spills of function pointers to the stack.
//
// Input:
//   MF - A reference to the MachineFunction to transform.
//
// Output:
//   MF - The transformed MachineFunction.
//
// Return value:
//   true  - The MachineFunction was transformed.
//   false - The MachineFunction was not transformed.
//
bool
ARMRandezvousICallLimiter::runOnMachineFunction(MachineFunction & MF) {
  if (!EnableRandezvousICallLimiter) {
    return false;
  }

  MachineRegisterInfo & MRI = MF.getRegInfo();
  const TargetInstrInfo * TII = MF.getSubtarget().getInstrInfo();

  // Find all indirect calls and limit the register they use to be within
  // { R0 -- R3, R12 } (i.e., tcGPR class).  This will ensure that those
  // function pointers are not spilled to memory due to callee-saved registers.
  bool changed = false;
  for (MachineBasicBlock & MBB : MF) {
    for (MachineInstr & MI : MBB) {
      if (MI.getOpcode() == ARM::tBLXr) {
        Register Reg = MI.getOperand(2).getReg();
        if (Reg.isVirtual()) {
          // Simply constrain the virtual register to be of tcGPR class
          MRI.constrainRegClass(Reg, &ARM::tcGPRRegClass);
          ++NumICallsLimited;
          changed = true;
        } else if (Reg.isPhysical() && !ARM::tcGPRRegClass.contains(Reg)) {
          // Need to create a virtual register of tcGPR class
          Register NewReg = MRI.createVirtualRegister(&ARM::tcGPRRegClass);
          MI.getOperand(2).setReg(NewReg);

          Register PredReg;
          ARMCC::CondCodes Pred = getInstrPredicate(MI, PredReg);
          if (Pred == ARMCC::AL) {
            // Build a COPY from the physical register to the new register
            BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(TargetOpcode::COPY),
                    NewReg)
            .addReg(Reg);
          } else {
            // If the call is predicated, use the target-specific MOV because
            // COPY cannot be predicated
            BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(ARM::tMOVr), NewReg)
            .addReg(Reg)
            .add(predOps(Pred, PredReg));
          }
          ++NumICallsLimited;
          changed = true;
        }
      }
    }
  }

  return changed;
}

FunctionPass *
llvm::createARMRandezvousICallLimiter(void) {
  return new ARMRandezvousICallLimiter();
}
