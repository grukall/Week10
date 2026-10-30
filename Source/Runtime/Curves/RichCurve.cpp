#include "EnginePCH.h"

#include "Curves/RichCurve.h"
#include "Math/Interp.h"

namespace
{
	int32 FindInsertIndex(const TArray<FRichCurveKey>& Keys, float Time)
	{
		int32 Index = 0;
		while (Index < Keys.Num() && Keys[Index].Time < Time)
			++Index;
		return Index;
	}

	// 언리얼 CurveEvaluation.cpp EvalForTwoKeys
	float EvalForTwoKeys(const FRichCurveKey& Key1, const FRichCurveKey& Key2, float Time)
	{
		const float Diff = Key2.Time - Key1.Time;
		if (Diff <= 0.f || Key1.InterpMode == ERichCurveInterpMode::Constant)
			return Key1.Value;

		const float Alpha = (Time - Key1.Time) / Diff;
		if (Key1.InterpMode == ERichCurveInterpMode::Linear)
			return FMath::Lerp(Key1.Value, Key2.Value, Alpha);

		// 기울기(값/초)를 Bezier 제어점으로 바꾼다. 구간 길이의 1/3 만큼 기울기 방향으로 뻗은 점
		const float P1 = Key1.Value + Key1.LeaveTangent * Diff / 3.f;
		const float P2 = Key2.Value - Key2.ArriveTangent * Diff / 3.f;
		return FMath::BezierInterp(Key1.Value, P1, P2, Key2.Value, Alpha);
	}
}

int32 FRichCurve::AddKey(float Time, float Value)
{
	const int32 Index = FindInsertIndex(Keys, Time);
	if (Index < Keys.Num() && FMath::IsNearlyEqual(Keys[Index].Time, Time))
	{
		Keys[Index].Value = Value;
	}
	else
	{
		FRichCurveKey Key;
		Key.Time = Time;
		Key.Value = Value;
		Keys.Insert(Key, Index);
	}
	AutoSetTangents();
	return Index;
}

void FRichCurve::DeleteKey(int32 Index)
{
	if (Index < 0 || Index >= Keys.Num())
		return;
	Keys.RemoveAt(Index);
	AutoSetTangents();
}

int32 FRichCurve::SetKeyTime(int32 Index, float NewTime)
{
	if (Index < 0 || Index >= Keys.Num())
		return Index;

	FRichCurveKey Key = Keys[Index];
	Keys.RemoveAt(Index);
	Key.Time = NewTime;
	const int32 NewIndex = FindInsertIndex(Keys, NewTime);
	Keys.Insert(Key, NewIndex);
	AutoSetTangents();
	return NewIndex;
}

void FRichCurve::SetKeyValue(int32 Index, float NewValue)
{
	if (Index < 0 || Index >= Keys.Num())
		return;
	Keys[Index].Value = NewValue;
	AutoSetTangents();
}

// 언리얼 FRichCurve::AutoSetTangents (RichCurve.cpp)
void FRichCurve::AutoSetTangents(float Tension)
{
	const int32 Num = Keys.Num();
	for (int32 KeyIndex = 0; KeyIndex < Num; ++KeyIndex)
	{
		FRichCurveKey& Key = Keys[KeyIndex];
		float ArriveTangent = Key.ArriveTangent;
		float LeaveTangent = Key.LeaveTangent;

		if (KeyIndex == 0)
		{
			// 첫 키: 평평하게 출발
			if (KeyIndex < Num - 1 && Key.TangentMode == ERichCurveTangentMode::Auto)
				LeaveTangent = 0.f;
		}
		else if (KeyIndex < Num - 1)
		{
			const FRichCurveKey& PrevKey = Keys[KeyIndex - 1];

			if (Key.InterpMode == ERichCurveInterpMode::Cubic && Key.TangentMode == ERichCurveTangentMode::Auto)
			{
				// 가운데 키: 앞 키와 뒤 키를 잇는 기울기 (언리얼 ComputeCurveTangent, 클램핑 없음)
				const FRichCurveKey& NextKey = Keys[KeyIndex + 1];
				const float PrevToNextTimeDiff = FMath::Max(KINDA_SMALL_NUMBER, NextKey.Time - PrevKey.Time);
				ArriveTangent = (1.f - Tension) * ((Key.Value - PrevKey.Value) + (NextKey.Value - Key.Value)) / PrevToNextTimeDiff;
				// Auto 모드에서는 들어오고 나가는 기울기가 같다
				LeaveTangent = ArriveTangent;
			}
			else if (PrevKey.InterpMode == ERichCurveInterpMode::Constant || Key.InterpMode == ERichCurveInterpMode::Constant)
			{
				LeaveTangent = 0.f;
				if (PrevKey.InterpMode != ERichCurveInterpMode::Cubic)
					ArriveTangent = 0.f;
			}
		}
		else
		{
			// 마지막 키: 평평하게 도착
			if (Key.InterpMode == ERichCurveInterpMode::Cubic && Key.TangentMode == ERichCurveTangentMode::Auto)
				ArriveTangent = 0.f;
		}

		Key.ArriveTangent = ArriveTangent;
		Key.LeaveTangent = LeaveTangent;
	}
}

float FRichCurve::Eval(float Time, float DefaultValue) const
{
	const int32 Num = Keys.Num();
	if (Num == 0)
		return DefaultValue;
	if (Time <= Keys[0].Time)
		return Keys[0].Value;
	if (Time >= Keys[Num - 1].Time)
		return Keys[Num - 1].Value;

	// Time 이 들어 있는 구간 [Lo, Hi] 를 이진 탐색
	int32 Lo = 0;
	int32 Hi = Num - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Keys[Mid].Time <= Time) Lo = Mid;
		else Hi = Mid;
	}
	return EvalForTwoKeys(Keys[Lo], Keys[Hi], Time);
}

void FRichCurve::GetTimeRange(float& OutMin, float& OutMax) const
{
	OutMin = Keys.IsEmpty() ? 0.f : Keys[0].Time;
	OutMax = Keys.IsEmpty() ? 0.f : Keys[Keys.Num() - 1].Time;
}

void FRichCurve::Serialize(json& Handle, bool bIsLoading)
{
	if (bIsLoading)
	{
		Keys.Empty();
		if (!Handle.contains("Keys") || !Handle["Keys"].is_array())
			return;

		for (const json& K : Handle["Keys"])
		{
			FRichCurveKey Key;
			Key.Time = K.value("Time", 0.f);
			Key.Value = K.value("Value", 0.f);
			Key.InterpMode = static_cast<ERichCurveInterpMode>(K.value("Interp", static_cast<int>(ERichCurveInterpMode::Linear)));
			Key.TangentMode = static_cast<ERichCurveTangentMode>(K.value("TangentMode", static_cast<int>(ERichCurveTangentMode::Auto)));
			Key.ArriveTangent = K.value("Arrive", 0.f);
			Key.LeaveTangent = K.value("Leave", 0.f);
			Keys.Add(Key);
		}
		AutoSetTangents();
	}
	else
	{
		json Array = json::array();
		for (const FRichCurveKey& Key : Keys)
		{
			Array.push_back({
				{ "Time", Key.Time },
				{ "Value", Key.Value },
				{ "Interp", static_cast<int>(Key.InterpMode) },
				{ "TangentMode", static_cast<int>(Key.TangentMode) },
				{ "Arrive", Key.ArriveTangent },
				{ "Leave", Key.LeaveTangent } });
		}
		Handle["Keys"] = Array;
	}
}
