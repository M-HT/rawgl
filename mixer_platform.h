
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef MIXER_PLATFORM_H__
#define MIXER_PLATFORM_H__

#include "intern.h"

#if ( \
    defined(__aarch64__) || \
    defined(_M_ARM64) || \
    defined(_M_ARM64EC) \
)
    #define ARMV8 1
#else
    #undef ARMV8
#endif

#if (!defined(ARMV8)) && ( \
    (defined(__ARM_ARCH) && (__ARM_ARCH >= 6)) || \
    (defined(_M_ARM) && (_M_ARM >= 6)) || \
    (defined(__TARGET_ARCH_ARM) && (__TARGET_ARCH_ARM >= 6)) || \
    (defined(__TARGET_ARCH_THUMB) && (__TARGET_ARCH_THUMB >= 3)) \
)
    #define ARMV6 1
#else
    #undef ARMV6
#endif

#if defined(ARMV8)
#include <arm_neon.h>
#elif defined(ARMV6) && defined(__ARM_ACLE) && __ARM_FEATURE_SAT
#include <arm_acle.h>
#endif

static inline int16_t mixS16(int sample1, int sample2) {
#if defined(ARMV8)
	return vqmovns_s32(sample1 + sample2);
#elif defined(ARMV6) && defined(__ARM_ACLE) && __ARM_FEATURE_SAT
	return __ssat(sample1 + sample2, 16);
#elif defined(ARMV6) && defined(__GNUC__)
	int sample;
	asm ( "ssat %[result], #16, %[value]" : [result] "=r" (sample) : [value] "r" (sample1 + sample2) : "cc" );
	return sample;
#else
	const int sample = sample1 + sample2;
	return sample < -32768 ? -32768 : ((sample > 32767 ? 32767 : sample));
#endif
}

#endif
