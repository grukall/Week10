#pragma once
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Curves/RichCurve.h"

// FRichCurve 하나를 에셋으로 감싼 것. 언리얼 UCurveFloat. Resources/Curves/*.json 으로 저장
class UCurveFloat : public UObject
{
	DECLARE_CLASS(UCurveFloat, UObject)
public:
	FRichCurve FloatCurve;

	float GetFloatValue(float Time) const { return FloatCurve.Eval(Time); }

	bool SaveToFile(const FString& Path);
	// 경로별로 캐시해 같은 파일은 같은 객체를 반환한다
	static UCurveFloat* LoadFromFile(const FString& Path);
};
