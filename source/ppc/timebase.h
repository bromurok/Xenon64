#ifndef PPC_TIMEBASE_H
#define PPC_TIMEBASE_H

#include <ppcintrinsics.h>

/* Xenon (Xbox 360 CPU) runs its Time Base Register at 50 MHz */
#define PPC_TIMEBASE_FREQ 50000000ULL

static __inline unsigned long long mftb(void)
{
    return __mftb();
}

#endif /* PPC_TIMEBASE_H */
