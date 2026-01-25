#ifndef _KOSM_COLOR_H
#define _KOSM_COLOR_H

#include <SupportDefs.h>
#include <math.h>

struct KosmColor;


enum kosm_color_space {
	KOSM_COLOR_SPACE_SRGB = 0,
	KOSM_COLOR_SPACE_LINEAR_SRGB,
	KOSM_COLOR_SPACE_DISPLAY_P3
};


struct KosmColor {
	float	r;
	float	g;
	float	b;
	float	a;

	// Constructors

	KosmColor() : r(0), g(0), b(0), a(1) {}

	KosmColor(float r, float g, float b, float a = 1.0f)
		: r(r), g(g), b(b), a(a) {}

	// From integer formats (8-bit per channel)

	static KosmColor FromRGBA8(uint8 r, uint8 g, uint8 b, uint8 a = 255) {
		return KosmColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
	}

	static KosmColor FromRGB8(uint8 r, uint8 g, uint8 b) {
		return FromRGBA8(r, g, b, 255);
	}

	static KosmColor FromGray8(uint8 gray, uint8 a = 255) {
		return FromRGBA8(gray, gray, gray, a);
	}

	// From packed integer formats

	static KosmColor FromRGBA32(uint32 rgba) {
		return FromRGBA8(
			(rgba >> 24) & 0xFF,
			(rgba >> 16) & 0xFF,
			(rgba >> 8) & 0xFF,
			rgba & 0xFF
		);
	}

	static KosmColor FromARGB32(uint32 argb) {
		return FromRGBA8(
			(argb >> 16) & 0xFF,
			(argb >> 8) & 0xFF,
			argb & 0xFF,
			(argb >> 24) & 0xFF
		);
	}

	static KosmColor FromBGRA32(uint32 bgra) {
		return FromRGBA8(
			(bgra >> 8) & 0xFF,
			(bgra >> 16) & 0xFF,
			(bgra >> 24) & 0xFF,
			bgra & 0xFF
		);
	}

	static KosmColor FromABGR32(uint32 abgr) {
		return FromRGBA8(
			abgr & 0xFF,
			(abgr >> 8) & 0xFF,
			(abgr >> 16) & 0xFF,
			(abgr >> 24) & 0xFF
		);
	}

	static KosmColor FromRGB24(uint32 rgb) {
		return FromRGBA8(
			(rgb >> 16) & 0xFF,
			(rgb >> 8) & 0xFF,
			rgb & 0xFF,
			255
		);
	}

	// Hex string style: 0xRRGGBBAA or 0xRRGGBB
	static KosmColor FromHex(uint32 hex, bool hasAlpha = true) {
		if (hasAlpha)
			return FromRGBA32(hex);
		return FromRGB24(hex);
	}

	// From HSL/HSV

	static KosmColor FromHSL(float h, float s, float l, float a = 1.0f) {
		// h in [0, 360), s and l in [0, 1]
		h = fmodf(h, 360.0f);
		if (h < 0) h += 360.0f;
		
		float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
		float hp = h / 60.0f;
		float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
		float m = l - c / 2.0f;

		float r1, g1, b1;
		if (hp < 1) { r1 = c; g1 = x; b1 = 0; }
		else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
		else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
		else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
		else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
		else { r1 = c; g1 = 0; b1 = x; }

		return KosmColor(r1 + m, g1 + m, b1 + m, a);
	}

	static KosmColor FromHSV(float h, float s, float v, float a = 1.0f) {
		// h in [0, 360), s and v in [0, 1]
		h = fmodf(h, 360.0f);
		if (h < 0) h += 360.0f;
		
		float c = v * s;
		float hp = h / 60.0f;
		float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
		float m = v - c;

		float r1, g1, b1;
		if (hp < 1) { r1 = c; g1 = x; b1 = 0; }
		else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
		else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
		else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
		else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
		else { r1 = c; g1 = 0; b1 = x; }

		return KosmColor(r1 + m, g1 + m, b1 + m, a);
	}

	// Predefined colors - Basic

	static KosmColor Transparent() { return KosmColor(0, 0, 0, 0); }
	static KosmColor Black() { return KosmColor(0, 0, 0, 1); }
	static KosmColor White() { return KosmColor(1, 1, 1, 1); }
	
	static KosmColor Red() { return KosmColor(1, 0, 0, 1); }
	static KosmColor Green() { return KosmColor(0, 1, 0, 1); }
	static KosmColor Blue() { return KosmColor(0, 0, 1, 1); }
	
	static KosmColor Yellow() { return KosmColor(1, 1, 0, 1); }
	static KosmColor Cyan() { return KosmColor(0, 1, 1, 1); }
	static KosmColor Magenta() { return KosmColor(1, 0, 1, 1); }

	// Predefined colors - Grays

	static KosmColor Gray() { return KosmColor(0.5f, 0.5f, 0.5f, 1); }
	static KosmColor Gray10() { return KosmColor(0.1f, 0.1f, 0.1f, 1); }
	static KosmColor Gray20() { return KosmColor(0.2f, 0.2f, 0.2f, 1); }
	static KosmColor Gray30() { return KosmColor(0.3f, 0.3f, 0.3f, 1); }
	static KosmColor Gray40() { return KosmColor(0.4f, 0.4f, 0.4f, 1); }
	static KosmColor Gray50() { return KosmColor(0.5f, 0.5f, 0.5f, 1); }
	static KosmColor Gray60() { return KosmColor(0.6f, 0.6f, 0.6f, 1); }
	static KosmColor Gray70() { return KosmColor(0.7f, 0.7f, 0.7f, 1); }
	static KosmColor Gray80() { return KosmColor(0.8f, 0.8f, 0.8f, 1); }
	static KosmColor Gray90() { return KosmColor(0.9f, 0.9f, 0.9f, 1); }
	
	static KosmColor DarkGray() { return KosmColor(0.25f, 0.25f, 0.25f, 1); }
	static KosmColor LightGray() { return KosmColor(0.75f, 0.75f, 0.75f, 1); }

	// Predefined colors - Extended palette

	static KosmColor Orange() { return KosmColor(1.0f, 0.5f, 0.0f, 1); }
	static KosmColor Pink() { return KosmColor(1.0f, 0.75f, 0.8f, 1); }
	static KosmColor Purple() { return KosmColor(0.5f, 0.0f, 0.5f, 1); }
	static KosmColor Brown() { return KosmColor(0.6f, 0.3f, 0.0f, 1); }
	static KosmColor Teal() { return KosmColor(0.0f, 0.5f, 0.5f, 1); }
	static KosmColor Navy() { return KosmColor(0.0f, 0.0f, 0.5f, 1); }
	static KosmColor Olive() { return KosmColor(0.5f, 0.5f, 0.0f, 1); }
	static KosmColor Maroon() { return KosmColor(0.5f, 0.0f, 0.0f, 1); }
	static KosmColor Lime() { return KosmColor(0.0f, 1.0f, 0.0f, 1); }
	static KosmColor Aqua() { return KosmColor(0.0f, 1.0f, 1.0f, 1); }
	static KosmColor Fuchsia() { return KosmColor(1.0f, 0.0f, 1.0f, 1); }
	static KosmColor Silver() { return KosmColor(0.75f, 0.75f, 0.75f, 1); }
	static KosmColor Coral() { return KosmColor(1.0f, 0.5f, 0.31f, 1); }
	static KosmColor Salmon() { return KosmColor(0.98f, 0.5f, 0.45f, 1); }
	static KosmColor Gold() { return KosmColor(1.0f, 0.84f, 0.0f, 1); }
	static KosmColor Indigo() { return KosmColor(0.29f, 0.0f, 0.51f, 1); }
	static KosmColor Violet() { return KosmColor(0.93f, 0.51f, 0.93f, 1); }
	static KosmColor Turquoise() { return KosmColor(0.25f, 0.88f, 0.82f, 1); }
	static KosmColor Crimson() { return KosmColor(0.86f, 0.08f, 0.24f, 1); }

	// Predefined colors - System/UI (iOS-inspired)

	static KosmColor SystemRed() { return KosmColor(1.0f, 0.23f, 0.19f, 1); }
	static KosmColor SystemOrange() { return KosmColor(1.0f, 0.58f, 0.0f, 1); }
	static KosmColor SystemYellow() { return KosmColor(1.0f, 0.8f, 0.0f, 1); }
	static KosmColor SystemGreen() { return KosmColor(0.2f, 0.78f, 0.35f, 1); }
	static KosmColor SystemMint() { return KosmColor(0.0f, 0.78f, 0.75f, 1); }
	static KosmColor SystemTeal() { return KosmColor(0.19f, 0.69f, 0.78f, 1); }
	static KosmColor SystemCyan() { return KosmColor(0.2f, 0.68f, 0.9f, 1); }
	static KosmColor SystemBlue() { return KosmColor(0.0f, 0.48f, 1.0f, 1); }
	static KosmColor SystemIndigo() { return KosmColor(0.35f, 0.34f, 0.84f, 1); }
	static KosmColor SystemPurple() { return KosmColor(0.69f, 0.32f, 0.87f, 1); }
	static KosmColor SystemPink() { return KosmColor(1.0f, 0.18f, 0.33f, 1); }
	static KosmColor SystemBrown() { return KosmColor(0.64f, 0.52f, 0.37f, 1); }

	// Semantic colors
	static KosmColor Label() { return Black(); }
	static KosmColor SecondaryLabel() { return KosmColor(0.24f, 0.24f, 0.26f, 0.6f); }
	static KosmColor TertiaryLabel() { return KosmColor(0.24f, 0.24f, 0.26f, 0.3f); }
	static KosmColor Separator() { return KosmColor(0.24f, 0.24f, 0.26f, 0.29f); }
	static KosmColor Link() { return SystemBlue(); }

	// Operators

	bool operator==(const KosmColor& other) const {
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}

	bool operator!=(const KosmColor& other) const {
		return !(*this == other);
	}

	KosmColor operator+(const KosmColor& other) const {
		return KosmColor(r + other.r, g + other.g, b + other.b, a + other.a);
	}

	KosmColor operator-(const KosmColor& other) const {
		return KosmColor(r - other.r, g - other.g, b - other.b, a - other.a);
	}

	KosmColor operator*(float scalar) const {
		return KosmColor(r * scalar, g * scalar, b * scalar, a * scalar);
	}

	KosmColor operator*(const KosmColor& other) const {
		return KosmColor(r * other.r, g * other.g, b * other.b, a * other.a);
	}

	// To integer formats (8-bit per channel)

	uint8 R8() const { return (uint8)(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f + 0.5f); }
	uint8 G8() const { return (uint8)(fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f + 0.5f); }
	uint8 B8() const { return (uint8)(fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f + 0.5f); }
	uint8 A8() const { return (uint8)(fminf(fmaxf(a, 0.0f), 1.0f) * 255.0f + 0.5f); }

	uint32 ToRGBA32() const {
		return (R8() << 24) | (G8() << 16) | (B8() << 8) | A8();
	}

	uint32 ToARGB32() const {
		return (A8() << 24) | (R8() << 16) | (G8() << 8) | B8();
	}

	uint32 ToBGRA32() const {
		return (B8() << 24) | (G8() << 16) | (R8() << 8) | A8();
	}

	uint32 ToABGR32() const {
		return (A8() << 24) | (B8() << 16) | (G8() << 8) | R8();
	}

	uint32 ToRGB24() const {
		return (R8() << 16) | (G8() << 8) | B8();
	}

	// To HSL/HSV

	void ToHSL(float* h, float* s, float* l) const {
		float maxC = fmaxf(fmaxf(r, g), b);
		float minC = fminf(fminf(r, g), b);
		float delta = maxC - minC;

		*l = (maxC + minC) * 0.5f;

		if (delta == 0) {
			*h = 0;
			*s = 0;
		} else {
			*s = delta / (1.0f - fabsf(2.0f * (*l) - 1.0f));
			
			if (maxC == r) {
				*h = 60.0f * fmodf((g - b) / delta, 6.0f);
			} else if (maxC == g) {
				*h = 60.0f * ((b - r) / delta + 2.0f);
			} else {
				*h = 60.0f * ((r - g) / delta + 4.0f);
			}
			if (*h < 0) *h += 360.0f;
		}
	}

	void ToHSV(float* h, float* s, float* v) const {
		float maxC = fmaxf(fmaxf(r, g), b);
		float minC = fminf(fminf(r, g), b);
		float delta = maxC - minC;

		*v = maxC;

		if (maxC == 0) {
			*s = 0;
			*h = 0;
		} else {
			*s = delta / maxC;
			
			if (delta == 0) {
				*h = 0;
			} else if (maxC == r) {
				*h = 60.0f * fmodf((g - b) / delta, 6.0f);
			} else if (maxC == g) {
				*h = 60.0f * ((b - r) / delta + 2.0f);
			} else {
				*h = 60.0f * ((r - g) / delta + 4.0f);
			}
			if (*h < 0) *h += 360.0f;
		}
	}

	// Queries

	bool IsOpaque() const {
		return a >= 1.0f;
	}

	bool IsTransparent() const {
		return a <= 0.0f;
	}

	bool IsGrayscale() const {
		return r == g && g == b;
	}

	float Luminance() const {
		// sRGB luminance coefficients (Rec. 709)
		return 0.2126f * r + 0.7152f * g + 0.0722f * b;
	}

	// Perceived brightness (faster approximation)
	float Brightness() const {
		return (r + r + r + b + g + g + g + g) / 8.0f;
	}

	// Modifications

	KosmColor WithAlpha(float newAlpha) const {
		return KosmColor(r, g, b, newAlpha);
	}

	KosmColor WithRed(float newRed) const {
		return KosmColor(newRed, g, b, a);
	}

	KosmColor WithGreen(float newGreen) const {
		return KosmColor(r, newGreen, b, a);
	}

	KosmColor WithBlue(float newBlue) const {
		return KosmColor(r, g, newBlue, a);
	}

	KosmColor Opaque() const {
		return WithAlpha(1.0f);
	}

	KosmColor Premultiplied() const {
		return KosmColor(r * a, g * a, b * a, a);
	}

	KosmColor Unpremultiplied() const {
		if (a == 0)
			return KosmColor(0, 0, 0, 0);
		return KosmColor(r / a, g / a, b / a, a);
	}

	KosmColor Clamped() const {
		return KosmColor(
			fminf(fmaxf(r, 0.0f), 1.0f),
			fminf(fmaxf(g, 0.0f), 1.0f),
			fminf(fmaxf(b, 0.0f), 1.0f),
			fminf(fmaxf(a, 0.0f), 1.0f)
		);
	}

	KosmColor Inverted() const {
		return KosmColor(1.0f - r, 1.0f - g, 1.0f - b, a);
	}

	KosmColor Grayscale() const {
		float l = Luminance();
		return KosmColor(l, l, l, a);
	}

	// Adjustments

	KosmColor Lightened(float amount) const {
		return KosmColor(
			r + (1.0f - r) * amount,
			g + (1.0f - g) * amount,
			b + (1.0f - b) * amount,
			a
		);
	}

	KosmColor Darkened(float amount) const {
		return KosmColor(
			r * (1.0f - amount),
			g * (1.0f - amount),
			b * (1.0f - amount),
			a
		);
	}

	KosmColor Saturated(float amount) const {
		float l = Luminance();
		return KosmColor(
			l + (r - l) * (1.0f + amount),
			l + (g - l) * (1.0f + amount),
			l + (b - l) * (1.0f + amount),
			a
		).Clamped();
	}

	KosmColor Desaturated(float amount) const {
		return Saturated(-amount);
	}

	KosmColor AdjustedHue(float degrees) const {
		float h, s, l;
		ToHSL(&h, &s, &l);
		return FromHSL(h + degrees, s, l, a);
	}

	// Blending

	KosmColor Lerp(const KosmColor& other, float t) const {
		return KosmColor(
			r + (other.r - r) * t,
			g + (other.g - g) * t,
			b + (other.b - b) * t,
			a + (other.a - a) * t
		);
	}

	KosmColor BlendOver(const KosmColor& background) const {
		// Porter-Duff source-over
		float outA = a + background.a * (1.0f - a);
		if (outA == 0)
			return KosmColor(0, 0, 0, 0);
		return KosmColor(
			(r * a + background.r * background.a * (1.0f - a)) / outA,
			(g * a + background.g * background.a * (1.0f - a)) / outA,
			(b * a + background.b * background.a * (1.0f - a)) / outA,
			outA
		);
	}

	KosmColor BlendMultiply(const KosmColor& other) const {
		return KosmColor(r * other.r, g * other.g, b * other.b, a * other.a);
	}

	KosmColor BlendScreen(const KosmColor& other) const {
		return KosmColor(
			1.0f - (1.0f - r) * (1.0f - other.r),
			1.0f - (1.0f - g) * (1.0f - other.g),
			1.0f - (1.0f - b) * (1.0f - other.b),
			1.0f - (1.0f - a) * (1.0f - other.a)
		);
	}

	KosmColor BlendOverlay(const KosmColor& other) const {
		auto overlay = [](float a, float b) {
			return a < 0.5f ? 2.0f * a * b : 1.0f - 2.0f * (1.0f - a) * (1.0f - b);
		};
		return KosmColor(
			overlay(r, other.r),
			overlay(g, other.g),
			overlay(b, other.b),
			a
		);
	}

	// Accessibility / Contrast

	float ContrastRatio(const KosmColor& other) const {
		float l1 = Luminance() + 0.05f;
		float l2 = other.Luminance() + 0.05f;
		return (l1 > l2) ? l1 / l2 : l2 / l1;
	}

	// WCAG 2.0 minimum contrast ratios:
	// - Normal text: 4.5:1 (AA), 7:1 (AAA)
	// - Large text: 3:1 (AA), 4.5:1 (AAA)
	bool HasSufficientContrast(const KosmColor& other, float minRatio = 4.5f) const {
		return ContrastRatio(other) >= minRatio;
	}

	// Returns black or white, whichever has better contrast
	KosmColor ContrastingBW() const {
		return Luminance() > 0.5f ? Black() : White();
	}

	// Color space conversion

	KosmColor ToLinear() const {
		// sRGB to linear conversion
		auto toLinear = [](float c) {
			if (c <= 0.04045f)
				return c / 12.92f;
			return powf((c + 0.055f) / 1.055f, 2.4f);
		};
		return KosmColor(toLinear(r), toLinear(g), toLinear(b), a);
	}

	KosmColor ToSRGB() const {
		// Linear to sRGB conversion
		auto toSRGB = [](float c) {
			if (c <= 0.0031308f)
				return c * 12.92f;
			return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
		};
		return KosmColor(toSRGB(r), toSRGB(g), toSRGB(b), a);
	}
};


inline KosmColor operator*(float scalar, const KosmColor& color) {
	return color * scalar;
}


#endif /* _KOSM_COLOR_H */
