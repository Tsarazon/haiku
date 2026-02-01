/*
 * Copyright 2013-2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Paweł Dziepak, pdziepak@quarnos.org
 *		Augustin Cavalier <waddlesplash>
 */


#include <util/Bitmap.h>

#include <stdlib.h>
#include <string.h>

#include <util/BitUtils.h>


namespace BKernel {


Bitmap::Bitmap(size_t bitCount)
	:
	fElementsCount(0),
	fSize(0),
	fBits(NULL)
{
	Resize(bitCount);
}


Bitmap::~Bitmap()
{
	free(fBits);
}


status_t
Bitmap::InitCheck()
{
	return (fBits != NULL) ? B_OK : B_NO_MEMORY;
}


status_t
Bitmap::Resize(size_t bitCount)
{
	const size_t count = (bitCount + kBitsPerElement - 1) / kBitsPerElement;
	if (count == fElementsCount) {
		fSize = bitCount;
		return B_OK;
	}

	void* bits = realloc(fBits, sizeof(addr_t) * count);
	if (bits == NULL)
		return B_NO_MEMORY;
	fBits = (addr_t*)bits;

	if (fElementsCount < count)
		memset(&fBits[fElementsCount], 0, sizeof(addr_t) * (count - fElementsCount));

	fSize = bitCount;
	fElementsCount = count;
	return B_OK;
}


void
Bitmap::Shift(ssize_t bitCount)
{
	return bitmap_shift<addr_t>(fBits, fSize, bitCount);
}


// Sets a contiguous range of bits.
// Optimized to operate on whole words where possible, O(range/bits_per_word).
void
Bitmap::SetRange(size_t index, size_t count)
{
	if (count == 0)
		return;

	ASSERT(index < fSize && index + count <= fSize);

	size_t endIndex = index + count;
	size_t startElem = index / kBitsPerElement;
	size_t endElem = (endIndex - 1) / kBitsPerElement;

	if (startElem == endElem) {
		size_t startBit = index % kBitsPerElement;
		size_t endBit = (endIndex - 1) % kBitsPerElement;
		addr_t mask = ((addr_t(1) << (endBit - startBit + 1)) - 1) << startBit;
		fBits[startElem] |= mask;
		return;
	}

	size_t startBit = index % kBitsPerElement;
	if (startBit != 0) {
		fBits[startElem] |= ~(addr_t(0)) << startBit;
		startElem++;
	}

	for (size_t i = startElem; i < endElem; i++)
		fBits[i] = ~(addr_t(0));

	size_t endBit = (endIndex - 1) % kBitsPerElement;
	if (endBit == kBitsPerElement - 1)
		fBits[endElem] = ~(addr_t(0));
	else
		fBits[endElem] |= (addr_t(1) << (endBit + 1)) - 1;
}


// Clears a contiguous range of bits.
// Optimized to operate on whole words where possible, O(range/bits_per_word).
void
Bitmap::ClearRange(size_t index, size_t count)
{
	if (count == 0)
		return;

	ASSERT(index < fSize && index + count <= fSize);

	size_t endIndex = index + count;
	size_t startElem = index / kBitsPerElement;
	size_t endElem = (endIndex - 1) / kBitsPerElement;

	if (startElem == endElem) {
		size_t startBit = index % kBitsPerElement;
		size_t endBit = (endIndex - 1) % kBitsPerElement;
		addr_t mask = ((addr_t(1) << (endBit - startBit + 1)) - 1) << startBit;
		fBits[startElem] &= ~mask;
		return;
	}

	size_t startBit = index % kBitsPerElement;
	if (startBit != 0) {
		fBits[startElem] &= (addr_t(1) << startBit) - 1;
		startElem++;
	}

	for (size_t i = startElem; i < endElem; i++)
		fBits[i] = 0;

	size_t endBit = (endIndex - 1) % kBitsPerElement;
	if (endBit == kBitsPerElement - 1)
		fBits[endElem] = 0;
	else
		fBits[endElem] &= ~((addr_t(1) << (endBit + 1)) - 1);
}


// Count trailing zeros - finds position of lowest set bit.
// Compiles to tzcnt/bsf on x86_64, rbit+clz on ARM64.
static inline int
bitmap_ctz(addr_t value)
{
	return __builtin_ctzll(value);
}


// Count leading zeros - finds position of highest set bit.
// Compiles to lzcnt/bsr on x86_64, clz on ARM64.
static inline int
bitmap_clz(addr_t value)
{
	return __builtin_clzll(value);
}


// Population count - counts number of set bits in a word.
// Compiles to popcnt on x86_64, sequence on ARM64.
static inline int
bitmap_popcount(addr_t value)
{
	return __builtin_popcountll(value);
}


// Returns the total number of set bits in the bitmap.
// Uses hardware popcount instruction for O(1) per word.
size_t
Bitmap::CountSet() const
{
	size_t count = 0;
	for (size_t i = 0; i < fElementsCount; i++)
		count += bitmap_popcount(fBits[i]);

	// Mask off any trailing bits beyond fSize in the last element
	size_t tailBits = fSize % kBitsPerElement;
	if (tailBits != 0 && fElementsCount > 0) {
		addr_t tailMask = ~((addr_t(1) << tailBits) - 1);
		count -= bitmap_popcount(fBits[fElementsCount - 1] & tailMask);
	}

	return count;
}


// Finds the lowest set bit starting from fromIndex.
// Uses hardware bit-scan instructions for O(1) per word instead of O(bits).
ssize_t
Bitmap::GetLowestSet(size_t fromIndex) const
{
	if (fromIndex >= fSize)
		return -1;

	size_t elemIndex = fromIndex / kBitsPerElement;
	size_t bitOffset = fromIndex % kBitsPerElement;

	addr_t masked = fBits[elemIndex];
	if (bitOffset != 0)
		masked &= ~((addr_t(1) << bitOffset) - 1);

	if (masked != 0) {
		size_t bit = bitmap_ctz(masked);
		size_t result = elemIndex * kBitsPerElement + bit;
		return (result < fSize) ? (ssize_t)result : -1;
	}

	for (++elemIndex; elemIndex < fElementsCount; ++elemIndex) {
		if (fBits[elemIndex] != 0) {
			size_t bit = bitmap_ctz(fBits[elemIndex]);
			size_t result = elemIndex * kBitsPerElement + bit;
			return (result < fSize) ? (ssize_t)result : -1;
		}
	}

	return -1;
}


// Finds the lowest clear bit starting from fromIndex.
// Uses hardware bit-scan instructions for O(1) per word instead of O(bits).
ssize_t
Bitmap::GetLowestClear(size_t fromIndex) const
{
	if (fromIndex >= fSize)
		return -1;

	size_t elemIndex = fromIndex / kBitsPerElement;
	size_t bitOffset = fromIndex % kBitsPerElement;

	addr_t inverted = ~fBits[elemIndex];
	if (bitOffset != 0)
		inverted &= ~((addr_t(1) << bitOffset) - 1);

	if (inverted != 0) {
		size_t bit = bitmap_ctz(inverted);
		size_t result = elemIndex * kBitsPerElement + bit;
		return (result < fSize) ? (ssize_t)result : -1;
	}

	for (++elemIndex; elemIndex < fElementsCount; ++elemIndex) {
		inverted = ~fBits[elemIndex];
		if (inverted != 0) {
			size_t bit = bitmap_ctz(inverted);
			size_t result = elemIndex * kBitsPerElement + bit;
			return (result < fSize) ? (ssize_t)result : -1;
		}
	}

	return -1;
}


// Finds the lowest contiguous run of 'count' clear bits starting from fromIndex.
// Optimized to check whole words at once: if all bits in a word are clear,
// skips the entire word in O(1) instead of checking each bit individually.
ssize_t
Bitmap::GetLowestContiguousClear(size_t count, size_t fromIndex) const
{
	if (count == 0)
		return fromIndex;

	if (fromIndex >= fSize)
		return -1;

	ssize_t index = GetLowestClear(fromIndex);
	while (index >= 0) {
		if ((size_t)index + count > fSize)
			return -1;

		size_t found = 1;
		while (found < count) {
			size_t checkPos = index + found;
			size_t elemIndex = checkPos / kBitsPerElement;
			size_t bitOffset = checkPos % kBitsPerElement;

			// Fast path: skip entire zero words at once
			if (bitOffset == 0
				&& count - found >= (size_t)kBitsPerElement) {
				if (fBits[elemIndex] == 0) {
					found += kBitsPerElement;
					continue;
				}
				// Word has set bits; find first set bit to know how far
				// we actually got
				found += bitmap_ctz(fBits[elemIndex]);
				break;
			}

			if (Get(checkPos))
				break;
			found++;
		}

		if (found >= count)
			return index;

		index = GetLowestClear(index + found + 1);
	}

	return -1;
}


// Finds the highest set bit in the bitmap.
// Uses hardware bit-scan instructions for O(1) per word.
ssize_t
Bitmap::GetHighestSet() const
{
	if (fElementsCount == 0)
		return -1;

	ssize_t i = fElementsCount - 1;
	while (i >= 0 && fBits[i] == 0)
		i--;

	if (i < 0)
		return -1;

	int bit = (kBitsPerElement - 1) - bitmap_clz(fBits[i]);
	ssize_t result = i * kBitsPerElement + bit;

	return (result < (ssize_t)fSize) ? result : -1;
}


// Finds the highest clear bit in the bitmap.
// Uses hardware bit-scan instructions for O(1) per word.
ssize_t
Bitmap::GetHighestClear() const
{
	if (fElementsCount == 0)
		return -1;

	// Handle the last element specially to mask off bits beyond fSize
	ssize_t i = fElementsCount - 1;
	size_t tailBits = fSize % kBitsPerElement;

	addr_t inverted = ~fBits[i];
	if (tailBits != 0)
		inverted &= (addr_t(1) << tailBits) - 1;

	if (inverted != 0) {
		int bit = (kBitsPerElement - 1) - bitmap_clz(inverted);
		return i * kBitsPerElement + bit;
	}

	for (--i; i >= 0; --i) {
		inverted = ~fBits[i];
		if (inverted != 0) {
			int bit = (kBitsPerElement - 1) - bitmap_clz(inverted);
			return i * kBitsPerElement + bit;
		}
	}

	return -1;
}


} // namespace BKernel
