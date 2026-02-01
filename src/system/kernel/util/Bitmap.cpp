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
#if __SIZEOF_POINTER__ == 8
	return __builtin_ctzll(value);
#else
	return __builtin_ctz(value);
#endif
}


// Count leading zeros - finds position of highest set bit.
// Compiles to lzcnt/bsr on x86_64, clz on ARM64.
static inline int
bitmap_clz(addr_t value)
{
#if __SIZEOF_POINTER__ == 8
	return __builtin_clzll(value);
#else
	return __builtin_clz(value);
#endif
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
			if (Get(index + found))
				break;
			found++;
		}

		if (found == count)
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


} // namespace BKernel
