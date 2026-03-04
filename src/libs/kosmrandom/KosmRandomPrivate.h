/*
 * Copyright 2026 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmRandom — internal implementation details
 *
 * References:
 *   [1] Vigna S. "splitmix64" — http://prng.di.unimi.it/splitmix64.c
 *   [2] Blackman/Vigna "xoshiro256**" — http://prng.di.unimi.it/xoshiro256starstar.c
 *   [3] Bernstein D.J. "ChaCha" — https://cr.yp.to/chacha.html
 *   [4] Lemire D. "Fast Random Integer Generation in an Interval"
 *       ACM Trans. Model. Comput. Simul. 29(1), 2019
 */

#ifndef KOSM_RANDOM_PRIVATE_H
#define KOSM_RANDOM_PRIVATE_H

#include "KosmRandom.h"
#include <cstring>

namespace kosmrandom {
namespace detail {


/* --- Bit manipulation helpers --- */

inline uint32_t
Rotl32(uint32_t x, int k)
{
	return (x << k) | (x >> (32 - k));
}

inline uint64_t
Rotl64(uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}


/* --- SplitMix64 (Vigna) ---
 *
 * Used exclusively for seed expansion. Given a single uint64,
 * produces a sequence of well-distributed values suitable for
 * initializing larger PRNG states.
 *
 * Period: 2^64. Passes BigCrush.
 */

struct SplitMix64 {
	uint64_t state;

	void Seed(uint64_t seed) { state = seed; }

	uint64_t Next()
	{
		uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}
};


/* --- xoshiro256** (Blackman/Vigna 2018) ---
 *
 * 256-bit state, 64-bit output. The "**" scrambler provides
 * excellent quality even in the lowest bits.
 * Period: 2^256 - 1.
 *
 * Passes TestU01 BigCrush, PractRand 32TB.
 * Jump function allows splitting into 2^128 non-overlapping
 * subsequences — ideal for parallel/threaded use.
 */

inline uint64_t
Xoshiro256NextImpl(Xoshiro256State* xs)
{
	const uint64_t result = Rotl64(xs->s[1] * 5, 7) * 9;
	const uint64_t t = xs->s[1] << 17;

	xs->s[2] ^= xs->s[0];
	xs->s[3] ^= xs->s[1];
	xs->s[1] ^= xs->s[2];
	xs->s[0] ^= xs->s[3];

	xs->s[2] ^= t;
	xs->s[3] = Rotl64(xs->s[3], 45);

	return result;
}

inline void
Xoshiro256SeedImpl(Xoshiro256State* xs, uint64_t seed)
{
	SplitMix64 sm;
	sm.Seed(seed);

	xs->s[0] = sm.Next();
	xs->s[1] = sm.Next();
	xs->s[2] = sm.Next();
	xs->s[3] = sm.Next();

	if (xs->s[0] == 0 && xs->s[1] == 0 && xs->s[2] == 0 && xs->s[3] == 0)
		xs->s[0] = 1;
}

inline void
Xoshiro256SeedFull(Xoshiro256State* xs,
	uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3)
{
	xs->s[0] = s0;
	xs->s[1] = s1;
	xs->s[2] = s2;
	xs->s[3] = s3;

	if (s0 == 0 && s1 == 0 && s2 == 0 && s3 == 0)
		xs->s[0] = 1;
}

/* Jump constants for xoshiro256** */
/* Equivalent to 2^128 calls to Next() */
inline constexpr uint64_t kXoshiro256JumpTable[4] = {
	0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
	0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
};

/* Equivalent to 2^192 calls to Next() */
inline constexpr uint64_t kXoshiro256LongJumpTable[4] = {
	0x76e15d3efefdcbbfULL, 0xc5004e441c522fb3ULL,
	0x77710069854ee241ULL, 0x39109bb02acbe635ULL
};

inline void
Xoshiro256JumpImpl(Xoshiro256State* xs, const uint64_t table[4])
{
	uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;

	for (int i = 0; i < 4; i++) {
		for (int b = 0; b < 64; b++) {
			if (table[i] & (1ULL << b)) {
				s0 ^= xs->s[0];
				s1 ^= xs->s[1];
				s2 ^= xs->s[2];
				s3 ^= xs->s[3];
			}
			Xoshiro256NextImpl(xs);
		}
	}

	xs->s[0] = s0;
	xs->s[1] = s1;
	xs->s[2] = s2;
	xs->s[3] = s3;
}


/* --- ChaCha20 (Bernstein) ---
 *
 * Stream cipher used as CSPRNG. 256-bit key, 96-bit nonce.
 * Generates 64 bytes per block. Counter increments automatically.
 *
 * Security: 256-bit security level, resistant to backtracking
 * (if buffer is zeroed after use).
 */

#define CHACHA20_QUARTERROUND(a, b, c, d) do { \
	a += b; d ^= a; d = Rotl32(d, 16);         \
	c += d; b ^= c; b = Rotl32(b, 12);         \
	a += b; d ^= a; d = Rotl32(d, 8);          \
	c += d; b ^= c; b = Rotl32(b, 7);          \
} while (0)

inline void
ChaCha20Block(const uint32_t input[16], uint32_t output[16])
{
	uint32_t x[16];
	memcpy(x, input, sizeof(x));

	for (int i = 0; i < 10; i++) {
		CHACHA20_QUARTERROUND(x[0], x[4], x[ 8], x[12]);
		CHACHA20_QUARTERROUND(x[1], x[5], x[ 9], x[13]);
		CHACHA20_QUARTERROUND(x[2], x[6], x[10], x[14]);
		CHACHA20_QUARTERROUND(x[3], x[7], x[11], x[15]);
		CHACHA20_QUARTERROUND(x[0], x[5], x[10], x[15]);
		CHACHA20_QUARTERROUND(x[1], x[6], x[11], x[12]);
		CHACHA20_QUARTERROUND(x[2], x[7], x[ 8], x[13]);
		CHACHA20_QUARTERROUND(x[3], x[4], x[ 9], x[14]);
	}

	for (int i = 0; i < 16; i++)
		output[i] = x[i] + input[i];
}

#undef CHACHA20_QUARTERROUND

inline void
ChaCha20Init(ChaCha20State* cc,
	const uint8_t key[32], const uint8_t nonce[12])
{
	cc->state[0] = 0x61707865;
	cc->state[1] = 0x3320646e;
	cc->state[2] = 0x79622d32;
	cc->state[3] = 0x6b206574;

	for (int i = 0; i < 8; i++) {
		cc->state[4 + i] = static_cast<uint32_t>(key[i * 4 + 0])
			| (static_cast<uint32_t>(key[i * 4 + 1]) << 8)
			| (static_cast<uint32_t>(key[i * 4 + 2]) << 16)
			| (static_cast<uint32_t>(key[i * 4 + 3]) << 24);
	}

	cc->state[12] = 0;

	for (int i = 0; i < 3; i++) {
		cc->state[13 + i] = static_cast<uint32_t>(nonce[i * 4 + 0])
			| (static_cast<uint32_t>(nonce[i * 4 + 1]) << 8)
			| (static_cast<uint32_t>(nonce[i * 4 + 2]) << 16)
			| (static_cast<uint32_t>(nonce[i * 4 + 3]) << 24);
	}

	cc->available = 0;
}

inline void
ChaCha20Refill(ChaCha20State* cc)
{
	uint32_t output[16];

	ChaCha20Block(cc->state, output);

	cc->state[12]++;
	if (cc->state[12] == 0) {
		/*
		 * Counter exhausted (2^32 blocks = 256 GB from one key/nonce).
		 * Re-key: derive new key+nonce from current block output so
		 * forward secrecy is preserved and the nonce stays intact.
		 * The first 32 bytes of output become the new key,
		 * bytes 32..43 become the new nonce, counter resets to 0.
		 */
		uint8_t newkey[32];
		uint8_t newnonce[12];
		for (int i = 0; i < 8; i++) {
			newkey[i * 4 + 0] = static_cast<uint8_t>(output[i]);
			newkey[i * 4 + 1] = static_cast<uint8_t>(output[i] >> 8);
			newkey[i * 4 + 2] = static_cast<uint8_t>(output[i] >> 16);
			newkey[i * 4 + 3] = static_cast<uint8_t>(output[i] >> 24);
		}
		for (int i = 0; i < 3; i++) {
			newnonce[i * 4 + 0] = static_cast<uint8_t>(output[8 + i]);
			newnonce[i * 4 + 1] = static_cast<uint8_t>(output[8 + i] >> 8);
			newnonce[i * 4 + 2] = static_cast<uint8_t>(output[8 + i] >> 16);
			newnonce[i * 4 + 3] = static_cast<uint8_t>(output[8 + i] >> 24);
		}
		ChaCha20Init(cc, newkey, newnonce);
		memset(newkey, 0, sizeof(newkey));
		memset(newnonce, 0, sizeof(newnonce));
	}

	for (int i = 0; i < 16; i++) {
		cc->buffer[i * 4 + 0] = static_cast<uint8_t>(output[i]);
		cc->buffer[i * 4 + 1] = static_cast<uint8_t>(output[i] >> 8);
		cc->buffer[i * 4 + 2] = static_cast<uint8_t>(output[i] >> 16);
		cc->buffer[i * 4 + 3] = static_cast<uint8_t>(output[i] >> 24);
	}

	memset(output, 0, sizeof(output));
	cc->available = 64;
}

inline void
ChaCha20SeedU64(ChaCha20State* cc, uint64_t seed)
{
	SplitMix64 sm;
	sm.Seed(seed);

	uint8_t key[32];
	uint8_t nonce[12];

	for (int i = 0; i < 4; i++) {
		uint64_t v = sm.Next();
		memcpy(key + i * 8, &v, 8);
	}

	uint64_t n0 = sm.Next();
	uint32_t n1 = static_cast<uint32_t>(sm.Next());
	memcpy(nonce, &n0, 8);
	memcpy(nonce + 8, &n1, 4);

	ChaCha20Init(cc, key, nonce);

	memset(key, 0, sizeof(key));
	memset(nonce, 0, sizeof(nonce));
}


/* --- Lemire's nearly-divisionless bounded random ---
 *
 * Generates uniform random in [0, range) without modulo bias.
 * Uses at most 2 random values on average (usually 1).
 * No division in the common case.
 *
 * Uses __uint128_t widening multiply (available on all KosmOS
 * targets: ARM64, x86_64, RISC-V 64).
 *
 * Reference: Lemire, "Fast Random Integer Generation in an Interval"
 */

inline uint64_t
BoundedU64Impl(uint64_t range, uint64_t (*rng)(void*), void* state)
{
	if (range == 0)
		return 0;

	__uint128_t m = static_cast<__uint128_t>(rng(state))
		* static_cast<__uint128_t>(range);
	auto l = static_cast<uint64_t>(m);

	if (l < range) {
		uint64_t t = (-range) % range;
		while (l < t) {
			m = static_cast<__uint128_t>(rng(state))
				* static_cast<__uint128_t>(range);
			l = static_cast<uint64_t>(m);
		}
	}

	return static_cast<uint64_t>(m >> 64);
}

/* Template version — allows inlining of the RNG call in hot paths */
template<typename RNG>
inline uint64_t
BoundedU64Impl(uint64_t range, RNG& rng)
{
	if (range == 0)
		return 0;

	__uint128_t m = static_cast<__uint128_t>(rng())
		* static_cast<__uint128_t>(range);
	auto l = static_cast<uint64_t>(m);

	if (l < range) {
		uint64_t t = (-range) % range;
		while (l < t) {
			m = static_cast<__uint128_t>(rng())
				* static_cast<__uint128_t>(range);
			l = static_cast<uint64_t>(m);
		}
	}

	return static_cast<uint64_t>(m >> 64);
}


/* --- Float conversion ---
 *
 * Convert uniform integers to uniform floats in [0, 1).
 * Uses the full mantissa precision of each type.
 *
 * float:  23-bit mantissa, use top 24 bits -> divide by 2^24
 * double: 52-bit mantissa, use top 53 bits -> divide by 2^53
 */

inline float
U32ToFloatImpl(uint32_t x)
{
	return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

inline double
U64ToDoubleImpl(uint64_t x)
{
	return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
}


/* --- Global state --- */

struct GlobalState {
	Xoshiro256State		normal;
	ChaCha20State		secure;
	bool				initialized;
};

extern GlobalState gState;

void EnsureInitialized();


} // namespace detail
} // namespace kosmrandom

#endif // KOSM_RANDOM_PRIVATE_H
