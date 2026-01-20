/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
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

	KosmColor() : r(0), g(0), b(0), a(1) {}

	KosmColor(float r, float g, float b, float a = 1.0f)
		: r(r), g(g), b(b), a(a) {}

	static KosmColor FromRGBA8(uint8 r, uint8 g, uint8 b, uint8 a = 255) {
		return KosmColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
	}

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

	static KosmColor FromHex(uint32 hex) {
		// Interprets as 0xRRGGBBAA
		return FromRGBA32(hex);
	}

	static KosmColor FromHSL(float h, float s, float l, float a = 1.0f) {
		// h in [0, 360), s and l in [0, 1]
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

	static KosmColor Transparent() { return KosmColor(0, 0, 0, 0); }
	static KosmColor Black() { return KosmColor(0, 0, 0, 1); }
	static KosmColor White() { return KosmColor(1, 1, 1, 1); }
	static KosmColor Red() { return KosmColor(1, 0, 0, 1); }
	static KosmColor Green() { return KosmColor(0, 1, 0, 1); }
	static KosmColor Blue() { return KosmColor(0, 0, 1, 1); }

	bool operator==(const KosmColor& other) const {
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}

	bool operator!=(const KosmColor& other) const {
		return !(*this == other);
	}

	uint8 R8() const { return (uint8)(r * 255.0f + 0.5f); }
	uint8 G8() const { return (uint8)(g * 255.0f + 0.5f); }
	uint8 B8() const { return (uint8)(b * 255.0f + 0.5f); }
	uint8 A8() const { return (uint8)(a * 255.0f + 0.5f); }

	uint32 ToRGBA32() const {
		return (R8() << 24) | (G8() << 16) | (B8() << 8) | A8();
	}

	uint32 ToARGB32() const {
		return (A8() << 24) | (R8() << 16) | (G8() << 8) | B8();
	}

	uint32 ToBGRA32() const {
		return (B8() << 24) | (G8() << 16) | (R8() << 8) | A8();
	}

	bool IsOpaque() const {
		return a >= 1.0f;
	}

	bool IsTransparent() const {
		return a <= 0.0f;
	}

	float Luminance() const {
		// sRGB luminance coefficients
		return 0.2126f * r + 0.7152f * g + 0.0722f * b;
	}

	KosmColor WithAlpha(float newAlpha) const {
		return KosmColor(r, g, b, newAlpha);
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

#endif /* _KOSM_COLOR_H */
