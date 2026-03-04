/*
 * Copyright 2026 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * KosmRandom — modern portable random number library
 *
 * Algorithms:
 *   Normal  — xoshiro256** (Blackman/Vigna), period 2^256-1
 *   Secure  — ChaCha20 CSPRNG (Bernstein) + OS entropy
 *
 * Seeding:
 *   SplitMix64 (Vigna) for state initialization
 *
 * Range selection:
 *   Lemire's nearly-divisionless method (no modulo bias)
 */

#ifndef KOSM_RANDOM_H
#define KOSM_RANDOM_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kosmrandom {


/* --- Entropy (pluggable) --- */

/*
 * Entropy source callback.
 * Must fill buf with len random bytes.
 * Returns 0 on success, -1 on failure.
 *
 * Example for KosmOS:
 *   int MyEntropy(void* buf, size_t len, void* cookie) {
 *       return secure_random_fill(buf, len);
 *   }
 *   kosmrandom::SetEntropySource(MyEntropy, nullptr);
 */
using EntropyFunc = int (*)(void* buf, size_t len, void* cookie);

void		SetEntropySource(EntropyFunc func, void* cookie);
int			EntropyFill(void* buf, size_t len);
uint64_t	EntropyU64();


/* --- Algorithm state (forward) --- */

namespace detail {

struct Xoshiro256State {
	uint64_t	s[4];
};

struct ChaCha20State {
	uint32_t	state[16];
	uint8_t		buffer[64];
	uint32_t	available;
};

} // namespace detail


/* --- Random: static global convenience --- */

/*
 * Global generators, auto-seeded on first use.
 * NOT thread-safe. For threads, create per-thread
 * RandomGenerator / SecureRandom instances.
 */
class Random {
public:
	static void		Seed(uint64_t seed);
	static void		SeedFromEntropy();

	/* Raw values */
	static uint32_t	U32();
	static uint64_t	U64();
	static uint32_t	SecureU32();
	static uint64_t	SecureU64();

	/* Bounded range [min, max] inclusive, no modulo bias */
	static uint64_t	Range(uint64_t min, uint64_t max);

	/* Floating point [0.0, 1.0) */
	static float	Float();
	static double	Double();

	/* Bool with probability [0.0, 1.0] */
	static bool		Bool(double probability = 0.5);

	/* Shuffle array in-place (Fisher-Yates) */
	template<typename T>
	static void		Shuffle(T* array, size_t count);

	/* Fill buffer with random bytes */
	static void		Fill(void* buf, size_t len);
	static void		SecureFill(void* buf, size_t len);

	/* Type-generic range */
	template<typename T>
	static T		Get(T min, T max);

private:
	Random() = delete;
};


/* --- RandomGenerator: per-instance xoshiro256** --- */

class RandomGenerator {
public:
	explicit RandomGenerator(uint64_t seed);
	RandomGenerator();

	uint64_t	Next();
	uint64_t	Range(uint64_t min, uint64_t max);
	double		Double();
	float		Float();

	void		Jump();
	void		LongJump();

	template<typename T>
	void		Shuffle(T* array, size_t count);

private:
	detail::Xoshiro256State fState;
};


/* --- SecureRandom: per-instance ChaCha20 --- */

class SecureRandom {
public:
	SecureRandom();
	explicit SecureRandom(const uint8_t key[32], const uint8_t nonce[12]);

	uint32_t	U32();
	uint64_t	U64();
	void		Fill(void* buf, size_t len);

private:
	detail::ChaCha20State fState;
};


/* --- Inline implementations --- */

namespace detail {

uint64_t	Xoshiro256Next(Xoshiro256State* xs);
uint32_t	ChaCha20NextU32(ChaCha20State* cc);
uint64_t	ChaCha20NextU64(ChaCha20State* cc);
void		ChaCha20Fill(ChaCha20State* cc, void* buf, size_t len);
uint64_t	BoundedU64(uint64_t range, uint64_t (*rng)(void*), void* state);
float		U32ToFloat(uint32_t x);
double		U64ToDouble(uint64_t x);

inline uint64_t Xoshiro256Callback(void* state)
{
	return Xoshiro256Next(static_cast<Xoshiro256State*>(state));
}

} // namespace detail


inline uint64_t
RandomGenerator::Next()
{
	return detail::Xoshiro256Next(&fState);
}

inline double
RandomGenerator::Double()
{
	return detail::U64ToDouble(Next());
}

inline float
RandomGenerator::Float()
{
	return detail::U32ToFloat(static_cast<uint32_t>(Next() >> 32));
}

template<typename T>
void
RandomGenerator::Shuffle(T* array, size_t count)
{
	if (count < 2)
		return;

	for (size_t i = count - 1; i > 0; i--) {
		uint64_t j = Range(0, i);
		if (j != i) {
			T tmp = array[i];
			array[i] = array[j];
			array[j] = tmp;
		}
	}
}

inline uint32_t
SecureRandom::U32()
{
	return detail::ChaCha20NextU32(&fState);
}

inline uint64_t
SecureRandom::U64()
{
	return detail::ChaCha20NextU64(&fState);
}

inline void
SecureRandom::Fill(void* buf, size_t len)
{
	detail::ChaCha20Fill(&fState, buf, len);
}


/* --- Random::Shuffle --- */

template<typename T>
void
Random::Shuffle(T* array, size_t count)
{
	if (count < 2)
		return;

	for (size_t i = count - 1; i > 0; i--) {
		uint64_t j = Range(0, i);
		if (j != i) {
			T tmp = array[i];
			array[i] = array[j];
			array[j] = tmp;
		}
	}
}


/* --- Random::Get specializations --- */

template<>
inline int32_t Random::Get<int32_t>(int32_t min, int32_t max)
{
	if (min > max) { int32_t t = min; min = max; max = t; }
	uint64_t range = static_cast<uint64_t>(
		static_cast<int64_t>(max) - static_cast<int64_t>(min));
	uint64_t offset = Range(0, range);
	// Avoid signed overflow: add via unsigned arithmetic, then bit_cast
	uint32_t umin;
	memcpy(&umin, &min, sizeof(umin));
	uint32_t result = umin + static_cast<uint32_t>(offset);
	int32_t out;
	memcpy(&out, &result, sizeof(out));
	return out;
}

template<>
inline uint32_t Random::Get<uint32_t>(uint32_t min, uint32_t max)
{
	if (min > max) { uint32_t t = min; min = max; max = t; }
	return static_cast<uint32_t>(Range(min, max));
}

template<>
inline int64_t Random::Get<int64_t>(int64_t min, int64_t max)
{
	if (min > max) { int64_t t = min; min = max; max = t; }
	// Cast to uint64_t before subtracting to avoid signed overflow
	uint64_t range = static_cast<uint64_t>(max) - static_cast<uint64_t>(min);
	uint64_t umin;
	memcpy(&umin, &min, sizeof(umin));
	uint64_t result = umin + Range(0, range);
	int64_t out;
	memcpy(&out, &result, sizeof(out));
	return out;
}

template<>
inline uint64_t Random::Get<uint64_t>(uint64_t min, uint64_t max)
{
	if (min > max) { uint64_t t = min; min = max; max = t; }
	return Range(min, max);
}

template<>
inline float Random::Get<float>(float min, float max)
{
	if (min > max) { float t = min; min = max; max = t; }
	return min + Float() * (max - min);
}

template<>
inline double Random::Get<double>(double min, double max)
{
	if (min > max) { double t = min; min = max; max = t; }
	return min + Double() * (max - min);
}


} // namespace kosmrandom

#endif // KOSM_RANDOM_H
