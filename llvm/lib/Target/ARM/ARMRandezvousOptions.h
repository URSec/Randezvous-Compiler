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

extern bool EnableRandezvousCLR;
extern bool EnableRandezvousBBLR;

extern uint64_t RandezvousCLRSeed;

extern size_t RandezvousMaxTextSize;

#endif
