//===- ARMRandezvousOptions.h - ARM Randezvous Command Line Options -------===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file declares the command line options for ARM Randezvous passes.
//
//===----------------------------------------------------------------------===//

#ifndef ARM_RANDEZVOUS_OPTIONS
#define ARM_RANDEZVOUS_OPTIONS

#include <cstddef>
#include <cstdint>

//===----------------------------------------------------------------------===//
// Randezvous pass enablers
//===----------------------------------------------------------------------===//

extern bool EnableRandezvousCLR;
extern bool EnableRandezvousBBLR;
extern bool EnableRandezvousBBCLR;
extern bool EnableRandezvousPicoXOM;
extern bool EnableRandezvousGDLR;
extern bool EnableRandezvousGRBG;
extern bool EnableRandezvousGlobalGuard;
extern bool EnableRandezvousShadowStack;
extern bool EnableRandezvousRAN;
extern bool EnableRandezvousLGPromote;
extern bool EnableRandezvousICallLimiter;

//===----------------------------------------------------------------------===//
// Randezvous pass seeds
//===----------------------------------------------------------------------===//

extern uint64_t RandezvousCLRSeed;
extern uint64_t RandezvousGDLRSeed;
extern uint64_t RandezvousShadowStackSeed;

//===----------------------------------------------------------------------===//
// Size options used by Randezvous passes
//===----------------------------------------------------------------------===//

extern size_t RandezvousMaxTextSize;
extern size_t RandezvousMaxRodataSize;
extern size_t RandezvousMaxDataSize;
extern size_t RandezvousMaxBssSize;
extern size_t RandezvousShadowStackSize;

//===----------------------------------------------------------------------===//
// Miscellaneous options used by Randezvous passes
//===----------------------------------------------------------------------===//

extern unsigned RandezvousShadowStackStrideLength;
extern unsigned RandezvousNumGlobalGuardCandidates;
extern uintptr_t RandezvousRNGAddress;

#endif
