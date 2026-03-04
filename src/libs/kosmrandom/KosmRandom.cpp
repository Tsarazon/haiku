/*
 * Copyright 2026 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmRandom — implementation
 */

#include "KosmRandomPrivate.h"

#include <cstring>

namespace kosmrandom {


/* ======================================================================
 * Entropy collection (pluggable)
 * ====================================================================== */

static EntropyFunc sEntropyFunc = nullptr;
static void* sEntropyCookie = nullptr;

void
SetEntropySource(EntropyFunc func, void* cookie)
{
	sEntropyFunc = func;
	sEntropyCookie = cookie;
}

int
EntropyFill(void* buf, size_t len)
{
	if (sEntropyFunc != nullptr)
		return sEntropyFunc(buf, len, sEntropyCookie);

	/*
	 * No entropy source registered.
	 * Best-effort: mix stack/heap addresses (ASLR gives some bits)
	 * and an incrementing counter. This is WEAK — caller should
	 * register a real source via SetEntropySource().
	 */
	static uint64_t counter = 0;
	detail::SplitMix64 sm;
	uint64_t seed = 0;

	seed ^= reinterpret_cast<uint64_t>(buf);
	seed ^= reinterpret_cast<uint64_t>(&seed);
	seed ^= reinterpret_cast<uint64_t>(&counter);
	seed ^= ++counter * 0x517cc1b727220a95ULL;

	sm.Seed(seed);

	auto* p = static_cast<uint8_t*>(buf);
	for (size_t i = 0; i < len; i += 8) {
		uint64_t val = sm.Next();
		size_t chunk = (len - i < 8) ? (len - i) : 8;
		memcpy(p + i, &val, chunk);
	}

	return 0;
}

uint64_t
EntropyU64()
{
	uint64_t val = 0;
	EntropyFill(&val, sizeof(val));
	return val;
}


/* ======================================================================
 * Detail: non-inline implementations
 * ====================================================================== */

namespace detail {

GlobalState gState = { { { 0 } }, { { 0 }, { 0 }, 0 }, false };


void
EnsureInitialized()
{
	if (gState.initialized)
		return;

	uint64_t entropy[4];
	EntropyFill(entropy, sizeof(entropy));

	Xoshiro256SeedImpl(&gState.normal, entropy[0]);

	uint8_t key[32];
	uint8_t nonce[12];
	memcpy(key, &entropy[1], 16);
	memcpy(key + 16, &entropy[2], 16);
	memset(nonce, 0, sizeof(nonce));
	memcpy(nonce, &entropy[3], 8);
	ChaCha20Init(&gState.secure, key, nonce);

	memset(key, 0, sizeof(key));
	memset(nonce, 0, sizeof(nonce));
	memset(entropy, 0, sizeof(entropy));

	gState.initialized = true;
}


uint64_t
Xoshiro256Next(Xoshiro256State* xs)
{
	return Xoshiro256NextImpl(xs);
}

uint32_t
ChaCha20NextU32(ChaCha20State* cc)
{
	if (cc->available < 4)
		ChaCha20Refill(cc);

	uint32_t offset = 64 - cc->available;
	uint32_t val;
	memcpy(&val, cc->buffer + offset, 4);
	cc->available -= 4;

	return val;
}

uint64_t
ChaCha20NextU64(ChaCha20State* cc)
{
	if (cc->available < 8)
		ChaCha20Refill(cc);

	uint32_t offset = 64 - cc->available;
	uint64_t val;
	memcpy(&val, cc->buffer + offset, 8);
	cc->available -= 8;

	return val;
}

void
ChaCha20Fill(ChaCha20State* cc, void* buf, size_t len)
{
	auto* p = static_cast<uint8_t*>(buf);

	while (len > 0) {
		if (cc->available == 0)
			ChaCha20Refill(cc);

		uint32_t offset = 64 - cc->available;
		uint32_t chunk = cc->available;
		if (chunk > len)
			chunk = static_cast<uint32_t>(len);

		memcpy(p, cc->buffer + offset, chunk);
		cc->available -= chunk;
		p += chunk;
		len -= chunk;
	}
}

uint64_t
BoundedU64(uint64_t range, uint64_t (*rng)(void*), void* state)
{
	return BoundedU64Impl(range, rng, state);
}

float
U32ToFloat(uint32_t x)
{
	return U32ToFloatImpl(x);
}

double
U64ToDouble(uint64_t x)
{
	return U64ToDoubleImpl(x);
}

} // namespace detail


/* ======================================================================
 * Random (static global)
 * ====================================================================== */

void
Random::Seed(uint64_t seed)
{
	detail::SplitMix64 sm;
	sm.Seed(seed);

	detail::Xoshiro256SeedImpl(&detail::gState.normal, sm.Next());
	detail::ChaCha20SeedU64(&detail::gState.secure, sm.Next());

	detail::gState.initialized = true;
}

void
Random::SeedFromEntropy()
{
	detail::gState.initialized = false;
	detail::EnsureInitialized();
}

uint32_t
Random::U32()
{
	detail::EnsureInitialized();
	return static_cast<uint32_t>(
		detail::Xoshiro256Next(&detail::gState.normal) >> 32);
}

uint64_t
Random::U64()
{
	detail::EnsureInitialized();
	return detail::Xoshiro256Next(&detail::gState.normal);
}

uint32_t
Random::SecureU32()
{
	detail::EnsureInitialized();
	return detail::ChaCha20NextU32(&detail::gState.secure);
}

uint64_t
Random::SecureU64()
{
	detail::EnsureInitialized();
	return detail::ChaCha20NextU64(&detail::gState.secure);
}

uint64_t
Random::Range(uint64_t min, uint64_t max)
{
	detail::EnsureInitialized();

	if (min > max) {
		uint64_t t = min;
		min = max;
		max = t;
	}

	if (min == max)
		return min;

	uint64_t range = max - min + 1;
	if (range == 0)
		return detail::Xoshiro256Next(&detail::gState.normal);

	return min + detail::BoundedU64Impl(range,
		detail::Xoshiro256Callback, &detail::gState.normal);
}

float
Random::Float()
{
	return detail::U32ToFloatImpl(U32());
}

double
Random::Double()
{
	return detail::U64ToDoubleImpl(U64());
}

bool
Random::Bool(double probability)
{
	if (probability <= 0.0)
		return false;
	if (probability >= 1.0)
		return true;
	return Double() < probability;
}

void
Random::Fill(void* buf, size_t len)
{
	detail::EnsureInitialized();

	auto* p = static_cast<uint8_t*>(buf);

	while (len >= 8) {
		uint64_t val = detail::Xoshiro256Next(&detail::gState.normal);
		memcpy(p, &val, 8);
		p += 8;
		len -= 8;
	}

	if (len > 0) {
		uint64_t val = detail::Xoshiro256Next(&detail::gState.normal);
		memcpy(p, &val, len);
	}
}

void
Random::SecureFill(void* buf, size_t len)
{
	detail::EnsureInitialized();
	detail::ChaCha20Fill(&detail::gState.secure, buf, len);
}


/* ======================================================================
 * RandomGenerator
 * ====================================================================== */

RandomGenerator::RandomGenerator(uint64_t seed)
{
	detail::Xoshiro256SeedImpl(&fState, seed);
}

RandomGenerator::RandomGenerator()
{
	detail::Xoshiro256SeedImpl(&fState, EntropyU64());
}

uint64_t
RandomGenerator::Range(uint64_t min, uint64_t max)
{
	if (min > max) {
		uint64_t t = min;
		min = max;
		max = t;
	}

	if (min == max)
		return min;

	uint64_t range = max - min + 1;
	if (range == 0)
		return detail::Xoshiro256Next(&fState);

	return min + detail::BoundedU64Impl(range,
		detail::Xoshiro256Callback, &fState);
}

void
RandomGenerator::Jump()
{
	detail::Xoshiro256JumpImpl(&fState, detail::kXoshiro256JumpTable);
}

void
RandomGenerator::LongJump()
{
	detail::Xoshiro256JumpImpl(&fState, detail::kXoshiro256LongJumpTable);
}


/* ======================================================================
 * SecureRandom
 * ====================================================================== */

SecureRandom::SecureRandom()
{
	uint8_t key[32];
	uint8_t nonce[12];

	EntropyFill(key, sizeof(key));
	EntropyFill(nonce, sizeof(nonce));
	detail::ChaCha20Init(&fState, key, nonce);

	memset(key, 0, sizeof(key));
	memset(nonce, 0, sizeof(nonce));
}

SecureRandom::SecureRandom(const uint8_t key[32], const uint8_t nonce[12])
{
	detail::ChaCha20Init(&fState, key, nonce);
}


} // namespace kosmrandom
