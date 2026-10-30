#pragma once
#include "Math/Color.h"

// 카메라 (POV) 가 채우고 렌더러가 읽음
struct FPostProcessSettings
{
    float VignetteIntensity = 0.4f;     // 0 이면 끔
};

// PCM 없이 화면 효과를 확인하기 위한 에디터 디버그 값
// PCM 구현되면 연결 후 삭제 예정
struct FScreenEffectDebug
{
    bool bOverride = false;
    float VignetteIntensity = 0.4f;
    FLinearColor FadeColor = FLinearColor(0, 0, 0, 1);
    float FadeAmount = 0.0f;
    float LetterBoxAspect = 2.35f;
    float LetterBoxAmount = 0.0f;
};