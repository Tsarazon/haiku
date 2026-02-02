/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Unicode case conversion tables and utility functions.
 * Header-only; currently included only by BString.
 */
#ifndef _UNICODE_UTILS_H
#define _UNICODE_UTILS_H


#include <SupportDefs.h>


// Continuous case ranges: upper codepoints in [start, end] map to
// lower codepoints by adding delta. Sorted by start.
struct _UnicodeContinuousRange {
	uint32	start;
	uint32	end;
	int32	delta;
};

static const _UnicodeContinuousRange kContinuousRanges[] = {
	{0x0041, 0x005A, 32},		// Basic Latin A-Z
	{0x00C0, 0x00D6, 32},		// Latin-1 Supplement: A-O with accents
	{0x00D8, 0x00DE, 32},		// Latin-1 Supplement: O-Thorn
	{0x0388, 0x038A, 37},		// Greek tonos
	{0x038E, 0x038F, 63},		// Greek tonos
	{0x0391, 0x03A1, 32},		// Greek Alpha-Rho
	{0x03A3, 0x03A9, 32},		// Greek Sigma-Omega
	{0x0400, 0x040F, 80},		// Cyrillic supplement
	{0x0410, 0x042F, 32},		// Cyrillic A-Ya
	{0x0531, 0x0556, 48},		// Armenian
	{0xFF21, 0xFF3A, 32},		// Fullwidth Latin
};

static const int32 kContinuousRangeCount
	= sizeof(kContinuousRanges) / sizeof(kContinuousRanges[0]);


// Alternating case ranges: within [start, end], uppercase and lowercase
// codepoints alternate. If evenIsUpper is true, even codepoints are
// uppercase and odd are lowercase; otherwise reversed.
struct _UnicodeAlternatingRange {
	uint32	start;
	uint32	end;
	bool	evenIsUpper;
};

static const _UnicodeAlternatingRange kAlternatingRanges[] = {
	// Latin Extended-A
	{0x0100, 0x012F, true},
	{0x0132, 0x0137, true},
	{0x0139, 0x0148, false},
	{0x014A, 0x0177, true},
	{0x0179, 0x017E, false},
	// Latin Extended-B (selected)
	{0x0182, 0x0185, true},
	{0x01A0, 0x01A5, true},
	{0x01B3, 0x01B6, false},
	{0x01CD, 0x01DC, false},
	{0x01DE, 0x01EF, true},
	{0x01F8, 0x021F, true},
	{0x0222, 0x0233, true},
	{0x0246, 0x024F, true},
	// Greek extended
	{0x0370, 0x0373, true},
	{0x03D8, 0x03EF, true},
	// Cyrillic extended
	{0x0460, 0x0481, true},
	{0x048A, 0x04BF, true},
	{0x04C1, 0x04CE, false},
	{0x04D0, 0x04FF, true},
};

static const int32 kAlternatingRangeCount
	= sizeof(kAlternatingRanges) / sizeof(kAlternatingRanges[0]);


// Individual case mappings not covered by ranges above.
// Sorted by 'from' for binary search.
struct _UnicodeCaseMapping {
	uint32	from;
	uint32	to;
};

static const _UnicodeCaseMapping kToLowerSpecial[] = {
	{0x0130, 0x0069},	// I with dot above -> i
	{0x0178, 0x00FF},	// Y with diaeresis
	{0x0181, 0x0253},
	{0x0186, 0x0254},
	{0x0187, 0x0188},
	{0x0189, 0x0256},
	{0x018A, 0x0257},
	{0x018B, 0x018C},
	{0x018E, 0x01DD},
	{0x018F, 0x0259},
	{0x0190, 0x025B},
	{0x0191, 0x0192},
	{0x0193, 0x0260},
	{0x0194, 0x0263},
	{0x0196, 0x0269},
	{0x0197, 0x0268},
	{0x0198, 0x0199},
	{0x019C, 0x026F},
	{0x019D, 0x0272},
	{0x019F, 0x0275},
	{0x01A7, 0x01A8},
	{0x01A9, 0x0283},
	{0x01AC, 0x01AD},
	{0x01AE, 0x0288},
	{0x01AF, 0x01B0},
	{0x01B7, 0x0292},
	{0x01B8, 0x01B9},
	{0x01BC, 0x01BD},
	{0x01C4, 0x01C6},
	{0x01C5, 0x01C6},
	{0x01C7, 0x01C9},
	{0x01C8, 0x01C9},
	{0x01CA, 0x01CC},
	{0x01CB, 0x01CC},
	{0x01F1, 0x01F3},
	{0x01F2, 0x01F3},
	{0x01F4, 0x01F5},
	{0x01F6, 0x0195},
	{0x01F7, 0x01BF},
	{0x0220, 0x019E},
	{0x0243, 0x0180},
	{0x0386, 0x03AC},
	{0x038C, 0x03CC},
	{0x03CF, 0x03D7},
	{0x03F4, 0x03B8},
	{0x03F7, 0x03F8},
	{0x03F9, 0x03F2},
	{0x03FA, 0x03FB},
};

static const int32 kToLowerSpecialCount
	= sizeof(kToLowerSpecial) / sizeof(kToLowerSpecial[0]);


static const _UnicodeCaseMapping kToUpperSpecial[] = {
	{0x00FF, 0x0178},
	{0x0131, 0x0049},	// dotless i -> I
	{0x017F, 0x0053},	// long s -> S
	{0x0180, 0x0243},
	{0x0188, 0x0187},
	{0x018C, 0x018B},
	{0x0192, 0x0191},
	{0x0195, 0x01F6},
	{0x0199, 0x0198},
	{0x019E, 0x0220},
	{0x01A8, 0x01A7},
	{0x01AD, 0x01AC},
	{0x01B0, 0x01AF},
	{0x01B9, 0x01B8},
	{0x01BD, 0x01BC},
	{0x01BF, 0x01F7},
	{0x01C6, 0x01C4},
	{0x01C9, 0x01C7},
	{0x01CC, 0x01CA},
	{0x01DD, 0x018E},
	{0x01F3, 0x01F1},
	{0x01F5, 0x01F4},
	{0x0253, 0x0181},
	{0x0254, 0x0186},
	{0x0256, 0x0189},
	{0x0257, 0x018A},
	{0x0259, 0x018F},
	{0x025B, 0x0190},
	{0x0260, 0x0193},
	{0x0263, 0x0194},
	{0x0268, 0x0197},
	{0x0269, 0x0196},
	{0x026F, 0x019C},
	{0x0272, 0x019D},
	{0x0275, 0x019F},
	{0x0283, 0x01A9},
	{0x0288, 0x01AE},
	{0x0292, 0x01B7},
	{0x03AC, 0x0386},
	{0x03CC, 0x038C},
	{0x03D7, 0x03CF},
	{0x03F2, 0x03F9},
	{0x03F8, 0x03F7},
	{0x03FB, 0x03FA},
};

static const int32 kToUpperSpecialCount
	= sizeof(kToUpperSpecial) / sizeof(kToUpperSpecial[0]);


static inline uint32
_UnicodeSpecialLookup(const _UnicodeCaseMapping* table, int32 count,
	uint32 codepoint)
{
	int32 low = 0;
	int32 high = count - 1;

	while (low <= high) {
		int32 mid = (low + high) / 2;
		if (table[mid].from < codepoint)
			low = mid + 1;
		else if (table[mid].from > codepoint)
			high = mid - 1;
		else
			return table[mid].to;
	}

	return codepoint;
}


// Unicode-aware case conversion. Returns the codepoint unchanged
// if no mapping exists.

static inline uint32
unicode_tolower(uint32 cp)
{
	// Continuous ranges
	for (int32 i = 0; i < kContinuousRangeCount; i++) {
		if (cp >= kContinuousRanges[i].start
			&& cp <= kContinuousRanges[i].end) {
			return cp + kContinuousRanges[i].delta;
		}
	}

	// Alternating ranges
	for (int32 i = 0; i < kAlternatingRangeCount; i++) {
		const _UnicodeAlternatingRange& r = kAlternatingRanges[i];
		if (cp >= r.start && cp <= r.end) {
			bool isUpper = r.evenIsUpper
				? (cp % 2 == 0) : (cp % 2 == 1);
			return isUpper ? cp + 1 : cp;
		}
	}

	// Individual mappings
	return _UnicodeSpecialLookup(kToLowerSpecial, kToLowerSpecialCount, cp);
}


static inline uint32
unicode_toupper(uint32 cp)
{
	// Continuous ranges (check lowercase side)
	for (int32 i = 0; i < kContinuousRangeCount; i++) {
		uint32 lowerStart = kContinuousRanges[i].start
			+ kContinuousRanges[i].delta;
		uint32 lowerEnd = kContinuousRanges[i].end
			+ kContinuousRanges[i].delta;
		if (cp >= lowerStart && cp <= lowerEnd)
			return cp - kContinuousRanges[i].delta;
	}

	// Alternating ranges
	for (int32 i = 0; i < kAlternatingRangeCount; i++) {
		const _UnicodeAlternatingRange& r = kAlternatingRanges[i];
		if (cp >= r.start && cp <= r.end) {
			bool isLower = r.evenIsUpper
				? (cp % 2 == 1) : (cp % 2 == 0);
			return isLower ? cp - 1 : cp;
		}
	}

	// Individual mappings
	return _UnicodeSpecialLookup(kToUpperSpecial, kToUpperSpecialCount, cp);
}


// Unicode-aware alphabetic character check. Covers Latin, Cyrillic,
// Greek, Armenian, Georgian, Arabic, Hebrew, CJK, Hangul, Kana.

static inline bool
unicode_isalpha(uint32 cp)
{
	// ASCII
	if ((cp >= 0x41 && cp <= 0x5A) || (cp >= 0x61 && cp <= 0x7A))
		return true;
	if (cp < 0x80)
		return false;

	// Latin-1 Supplement (letters only, skip multiply and division signs)
	if (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7)
		return true;

	// Latin Extended-A, B
	if (cp >= 0x0100 && cp <= 0x024F)
		return true;

	// IPA Extensions
	if (cp >= 0x0250 && cp <= 0x02AF)
		return true;

	// Greek and Coptic
	if (cp >= 0x0370 && cp <= 0x03FF)
		return true;

	// Cyrillic
	if (cp >= 0x0400 && cp <= 0x04FF)
		return true;

	// Cyrillic Supplement
	if (cp >= 0x0500 && cp <= 0x052F)
		return true;

	// Armenian
	if (cp >= 0x0531 && cp <= 0x0587)
		return true;

	// Hebrew (letters)
	if (cp >= 0x05D0 && cp <= 0x05EA)
		return true;

	// Arabic (letters)
	if (cp >= 0x0620 && cp <= 0x064A)
		return true;
	if (cp >= 0x066E && cp <= 0x06D3)
		return true;

	// Thai
	if (cp >= 0x0E01 && cp <= 0x0E3A)
		return true;

	// Georgian
	if (cp >= 0x10A0 && cp <= 0x10FF)
		return true;
	if (cp >= 0x1C90 && cp <= 0x1CBF)
		return true;

	// Hangul Jamo
	if (cp >= 0x1100 && cp <= 0x11FF)
		return true;

	// Hiragana
	if (cp >= 0x3041 && cp <= 0x3096)
		return true;

	// Katakana
	if (cp >= 0x30A1 && cp <= 0x30FA)
		return true;

	// CJK Unified Ideographs
	if (cp >= 0x4E00 && cp <= 0x9FFF)
		return true;

	// Hangul Syllables
	if (cp >= 0xAC00 && cp <= 0xD7AF)
		return true;

	// Fullwidth Latin
	if ((cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A))
		return true;

	return false;
}


#endif	// _UNICODE_UTILS_H
