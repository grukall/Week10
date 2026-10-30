#include "EnginePCH.h"
#include "Color.h"


const FLinearColor FLinearColor::White(1.f, 1.f, 1.f);
const FLinearColor FLinearColor::Gray(0.5f, 0.5f, 0.5f);
const FLinearColor FLinearColor::Black(0, 0, 0);
const FLinearColor FLinearColor::Transparent(0, 0, 0, 0);
const FLinearColor FLinearColor::Red(1.f, 0, 0);
const FLinearColor FLinearColor::Green(0, 1.f, 0);
const FLinearColor FLinearColor::Blue(0, 0, 1.f);
const FLinearColor FLinearColor::Yellow(1.f, 1.f, 0);

const FColor FColor::White(255, 255, 255);
const FColor FColor::Black(0, 0, 0);
const FColor FColor::Transparent(0, 0, 0, 0);
const FColor FColor::Red(255, 0, 0);
const FColor FColor::Green(0, 255, 0);
const FColor FColor::Blue(0, 0, 255);
const FColor FColor::Yellow(255, 255, 0);
const FColor FColor::Cyan(0, 255, 255);
const FColor FColor::Magenta(255, 0, 255);
const FColor FColor::Orange(243, 156, 18);
const FColor FColor::Purple(169, 7, 228);
const FColor FColor::Turquoise(26, 188, 156);
const FColor FColor::Silver(189, 195, 199);
const FColor FColor::Emerald(46, 204, 113);

FLinearColor::FLinearColor(const FColor& C) : R(SRGBToLinear(C.R / 255.f)), G(SRGBToLinear(C.G / 255.f)), B(SRGBToLinear(C.B / 255.f)), A(C.A / 255.f) {}

// true = 감마 변환
FColor FLinearColor::ToFColor(bool bSRGB) const
{
	auto ToUint8 = [](float V)
		{
			V = V < 0.f ? 0.f : (V > 1.f ? 1.f : V);
			return static_cast<uint8>(V * 255.f + 0.5f);
		};

	auto ToColor = [&](float V) {
		V = bSRGB ? LinearToSRGB(V) : V;
		return ToUint8(V);
		};

	return FColor(ToColor(R), ToColor(G), ToColor(B), ToUint8(A));
}

FLinearColor FLinearColor::operator*(float S) const
{
	return FLinearColor(R * S, G * S, B * S, A * S);
}

float FLinearColor::SRGBToLinear(float C)
{
	return C <= 0.04045f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
}

float FLinearColor::LinearToSRGB(float L)
{
	return L <= 0.0031308f ? L * 12.92f : 1.055f * std::pow(L, 1.0f / 2.4f) - 0.055f;
}

// 감마 변환 없음
FLinearColor FColor::ReinterpretAsLinear() const
{
	return FLinearColor(R / 255.f, G / 255.f, B / 255.f, A / 255.f);
}
