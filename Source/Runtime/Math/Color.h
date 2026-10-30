#pragma once

#include <cmath>

#include "Core/Types.h"


struct FColor;

struct FLinearColor
{
	float R = 0.f, G = 0.f, B = 0.f, A = 1.f;

	FLinearColor() = default;
	constexpr FLinearColor(float InR, float InG, float InB, float InA = 1.f) : R(InR), G(InG), B(InB), A(InA) {}
	explicit FLinearColor(const FColor& C);

	FColor ToFColor(bool bSRGB) const;
	FLinearColor operator*(float S) const;
	static float SRGBToLinear(float C);
	static float LinearToSRGB(float L);

	// Common colors.	
	static const FLinearColor White;
	static const FLinearColor Gray;
	static const FLinearColor Black;
	static const FLinearColor Transparent;
	static const FLinearColor Red;
	static const FLinearColor Green;
	static const FLinearColor Blue;
	static const FLinearColor Yellow;
};

struct FColor
{
	uint8 B = 0, G = 0, R = 0, A = 255;

	FColor() = default;
	constexpr FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255) : B(InB), G(InG), R(InR), A(InA) {}

	FLinearColor ReinterpretAsLinear() const;

	/** Some pre-inited colors, useful for debug code */
	static const FColor White;
	static const FColor Black;
	static const FColor Transparent;
	static const FColor Red;
	static const FColor Green;
	static const FColor Blue;
	static const FColor Yellow;
	static const FColor Cyan;
	static const FColor Magenta;
	static const FColor Orange;
	static const FColor Purple;
	static const FColor Turquoise;
	static const FColor Silver;
	static const FColor Emerald;
};
