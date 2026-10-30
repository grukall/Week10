#pragma once

// 이 키에서 다음 키까지 어떻게 이을지
enum class ERichCurveInterpMode : uint8
{
	Linear,		// 직선
	Constant,	// 다음 키까지 값 유지 (계단)
	Cubic,		// 기울기를 써서 부드러운 곡선
};

enum class ERichCurveTangentMode : uint8
{
	Auto,		// 앞뒤 키를 보고 자동 계산
	User,		// 에디터에서 사용자가 지정
};

struct FRichCurveKey
{
	float Time = 0.f;
	float Value = 0.f;
	ERichCurveInterpMode InterpMode = ERichCurveInterpMode::Linear;		// 언리얼 FRichCurveKey 기본값
	ERichCurveTangentMode TangentMode = ERichCurveTangentMode::Auto;
	float ArriveTangent = 0.f;		// 들어오는 기울기 (값 / 초)
	float LeaveTangent = 0.f;		// 나가는 기울기 (값 / 초)
};

// 시간 → 값 키프레임 커브. 언리얼 FRichCurve
struct FRichCurve
{
	TArray<FRichCurveKey> Keys;		// Time 오름차순 유지

	// 같은 시간에 키가 있으면 값만 바꾼다. 들어간 인덱스를 반환
	int32 AddKey(float Time, float Value);
	void DeleteKey(int32 Index);
	// 시간이 바뀌면 순서가 바뀔 수 있어 새 인덱스를 반환
	int32 SetKeyTime(int32 Index, float NewTime);
	void SetKeyValue(int32 Index, float NewValue);
	// Auto 키의 기울기를 다시 계산한다. Tension 1이면 기울기가 0이 된다
	void AutoSetTangents(float Tension = 0.f);

	// 범위 밖은 양 끝 키 값을 유지한다 (언리얼 기본 Extrapolation = Constant)
	float Eval(float Time, float DefaultValue = 0.f) const;
	void GetTimeRange(float& OutMin, float& OutMax) const;

	void Serialize(json& Handle, bool bIsLoading);
};
