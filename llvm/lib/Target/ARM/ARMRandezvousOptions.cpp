//===- ARMRandezvousOptions.cpp - ARM Randezvous Command Line Options -----===//
//
// This file was written by at the University of Rochester.
// All Rights Reserved.
//
//===----------------------------------------------------------------------===//
//
// This file defines the command line options for ARM Randezvous passes.
//
//===----------------------------------------------------------------------===//

#include "ARMRandezvousOptions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

bool EnableRandezvousCLR;
static cl::opt<bool, true>
CLR("arm-randezvous-clr",
    cl::Hidden,
    cl::desc("Enable ARM Randezvous Code Layout Randomization"),
    cl::location(EnableRandezvousCLR),
    cl::init(false));

bool EnableRandezvousBBLR;
static cl::opt<bool, true>
BBLR("arm-randezvous-bblr",
     cl::Hidden,
     cl::desc("Enable Basic Block Layout Randomization for ARM Randezvous CLR"),
     cl::location(EnableRandezvousBBLR),
     cl::init(false));

uint64_t RandezvousCLRSeed;
static cl::opt<uint64_t, true>
CLRSeed("arm-randezvous-clr-seed",
        cl::Hidden,
        cl::desc("Seed for the RNG used in ARM Randezvous CLR"),
        cl::location(RandezvousCLRSeed),
        cl::init(0));

size_t RandezvousMaxTextSize;
static cl::opt<size_t, true>
MaxTextSize("arm-randezvous-max-text-size",
            cl::Hidden,
            cl::desc("Maximum text section size in bytes"),
            cl::location(RandezvousMaxTextSize),
            cl::init(0x1e0000));   // 2 MB - 128 KB
