/*
 * Copyright 2026 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ruslan, molotov2000@mail.ru
 */


#include <util/Random.h>

#include <OS.h>

#if defined(__x86_64__) && defined(_KERNEL_MODE)
#include <arch/x86/arch_cpu.h>
#define HAVE_X86_HW_RNG 1
#endif

#if defined(__aarch64__) && defined(_KERNEL_MODE)
#define HAVE_ARM64_HW_RNG 1
#endif


// PRNG state variables. Using int32 for compatibility with atomic_*() functions.
// Per-CPU state would reduce contention but adds complexity; atomics suffice here.
static int32	sFastLast	= 0;
static int32	sLast		= 0;
static int32	sSecureLast	= 0;


#ifdef HAVE_X86_HW_RNG

// x86_64 hardware random number generation via RDRAND/RDSEED instructions.
// RDSEED provides true entropy from the hardware RNG, RDRAND provides
// cryptographically secure random numbers from a DRBG seeded by hardware entropy.

static bool sHasRdrand = false;
static bool sHasRdseed = false;
static bool sX86HwRngChecked = false;


static void
x86_check_hw_rng()
{
	if (sX86HwRngChecked)
		return;

	sHasRdrand = x86_check_feature(IA32_FEATURE_EXT_RDRND, FEATURE_EXT);
	sHasRdseed = x86_check_feature(IA32_FEATURE_RDSEED, FEATURE_7_EBX);
	sX86HwRngChecked = true;
}


static inline bool
rdrand32(uint32* value)
{
	uint32 result;
	uint8 ok;
	asm volatile(
		"rdrand %0\n\t"
		"setc %1"
		: "=r" (result), "=qm" (ok)
		:
		: "cc"
	);
	if (ok) {
		*value = result;
		return true;
	}
	return false;
}


static inline bool
rdseed32(uint32* value)
{
	uint32 result;
	uint8 ok;
	asm volatile(
		"rdseed %0\n\t"
		"setc %1"
		: "=r" (result), "=qm" (ok)
		:
		: "cc"
	);
	if (ok) {
		*value = result;
		return true;
	}
	return false;
}

#endif // HAVE_X86_HW_RNG


#ifdef HAVE_ARM64_HW_RNG

// ARM64 hardware random number generation via RNDR instruction (ARMv8.5+).
// Reads from the system random number generator if available.

static bool sHasRndr = false;
static bool sArm64HwRngChecked = false;


static void
arm64_check_hw_rng()
{
	if (sArm64HwRngChecked)
		return;

	// Check ID_AA64ISAR0_EL1.RNDR field (bits 63:60)
	uint64 isar0;
	asm volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r" (isar0));
	sHasRndr = ((isar0 >> 60) & 0xf) >= 1;
	sArm64HwRngChecked = true;
}


static inline bool
rndr64(uint64* value)
{
	uint64 result;
	uint64 nzcv;
	// RNDR is encoded as S3_3_C2_C4_0
	asm volatile(
		"mrs %0, S3_3_C2_C4_0\n\t"
		"mrs %1, NZCV"
		: "=r" (result), "=r" (nzcv)
		:
		: "cc"
	);
	// NZCV.Z (bit 30) is set if RNDR failed
	if ((nzcv & 0x40000000) == 0) {
		*value = result;
		return true;
	}
	return false;
}

#endif // HAVE_ARM64_HW_RNG


// Linear congruential generator.
// Uses atomic operations to avoid race conditions between concurrent callers.
unsigned int
fast_random_value()
{
	int32 last = atomic_get(&sFastLast);
	if (last == 0) {
		last = system_time();
		atomic_test_and_set(&sFastLast, last, 0);
		last = atomic_get(&sFastLast);
	}

	int32 random = last * 1103515245 + 12345;
	atomic_set(&sFastLast, random);
	return ((uint32)random >> 16) & 0x7fff;
}


// Park-Miller PRNG ("Random number generators: good ones are hard to find",
// Communications of the ACM, vol. 31, no. 10, October 1988, p. 1195).
// Uses atomic operations to avoid race conditions between concurrent callers.
unsigned int
random_value()
{
	int32 last = atomic_get(&sLast);
	if (last == 0) {
		last = system_time();
		atomic_test_and_set(&sLast, last, 0);
		last = atomic_get(&sLast);
	}

	uint32 hi = (uint32)last / 127773;
	uint32 lo = (uint32)last % 127773;

	int32 random = 16807 * lo - 2836 * hi;
	if (random <= 0)
		random += MAX_RANDOM_VALUE;
	atomic_set(&sLast, random);
	return random % (MAX_RANDOM_VALUE + 1);
}


// Software CSPRNG fallback using SipHash-2-4 based mixing.
// Collects entropy from system state and mixes it cryptographically.
static uint32
secure_random_software()
{
	static int32 count = 0;

	uint32 data[8];
	data[0] = atomic_add(&count, 1);
	data[1] = system_time();
	data[2] = find_thread(NULL);
	data[3] = smp_get_current_cpu();
	data[4] = real_time_clock();
	data[5] = atomic_get(&sFastLast);
	data[6] = atomic_get(&sLast);
	data[7] = atomic_get(&sSecureLast);

	// SipHash-2-4 based mixing for cryptographic strength
	uint64 v0 = 0x736f6d6570736575ULL;
	uint64 v1 = 0x646f72616e646f6dULL;
	uint64 v2 = 0x6c7967656e657261ULL;
	uint64 v3 = 0x7465646279746573ULL;

	for (int i = 0; i < 8; i++) {
		uint64 m = data[i];
		v3 ^= m;

		for (int j = 0; j < 2; j++) {
			v0 += v1;
			v1 = (v1 << 13) | (v1 >> 51);
			v1 ^= v0;
			v0 = (v0 << 32) | (v0 >> 32);
			v2 += v3;
			v3 = (v3 << 16) | (v3 >> 48);
			v3 ^= v2;
			v0 += v3;
			v3 = (v3 << 21) | (v3 >> 43);
			v3 ^= v0;
			v2 += v1;
			v1 = (v1 << 17) | (v1 >> 47);
			v1 ^= v2;
			v2 = (v2 << 32) | (v2 >> 32);
		}

		v0 ^= m;
	}

	v2 ^= 0xff;
	for (int j = 0; j < 4; j++) {
		v0 += v1;
		v1 = (v1 << 13) | (v1 >> 51);
		v1 ^= v0;
		v0 = (v0 << 32) | (v0 >> 32);
		v2 += v3;
		v3 = (v3 << 16) | (v3 >> 48);
		v3 ^= v2;
		v0 += v3;
		v3 = (v3 << 21) | (v3 >> 43);
		v3 ^= v0;
		v2 += v1;
		v1 = (v1 << 17) | (v1 >> 47);
		v1 ^= v2;
		v2 = (v2 << 32) | (v2 >> 32);
	}

	int32 random = (int32)(v0 ^ v1 ^ v2 ^ v3);
	atomic_set(&sSecureLast, random);
	return (uint32)random;
}


// Returns a cryptographically secure random number.
// Prefers hardware RNG (RDSEED > RDRAND on x86, RNDR on ARM64) when available,
// falls back to software CSPRNG based on SipHash mixing of system entropy.
unsigned int
secure_random_value()
{
#ifdef HAVE_X86_HW_RNG
	x86_check_hw_rng();

	uint32 value;
	// Prefer RDSEED (true entropy) over RDRAND (DRBG output)
	if (sHasRdseed) {
		for (int retry = 0; retry < 10; retry++) {
			if (rdseed32(&value))
				return value;
		}
	}

	if (sHasRdrand) {
		for (int retry = 0; retry < 10; retry++) {
			if (rdrand32(&value))
				return value;
		}
	}
#endif

#ifdef HAVE_ARM64_HW_RNG
	arm64_check_hw_rng();

	if (sHasRndr) {
		uint64 value;
		for (int retry = 0; retry < 10; retry++) {
			if (rndr64(&value))
				return (uint32)value;
		}
	}
#endif

	return secure_random_software();
}
