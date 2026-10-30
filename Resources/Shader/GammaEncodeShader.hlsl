// Linear 씬 컬러(float)를 톤매핑한 뒤 화면 표시용 sRGB(8비트)로 인코딩한다.
// 라이팅·블렌딩·안개는 모두 Linear에서 계산하고, 화면에 내보내기 직전에 한 번만 감마를 적용한다.
Texture2D SceneColor : register(t0);

cbuffer GammaEncodeConstants : register(b0)
{
    // 0이면 WorldNormal처럼 값 자체가 데이터인 뷰라서 변환 없이 그대로 옮긴다.
    uint bEncode;
    // 조명 결과의 HDR 범위를 줄이는 단계라 Lit 뷰에서만 켠다. Unlit은 저작한 색이 그대로 보여야 한다.
    uint bToneMap;
    // 톤매핑 직전에 곱하는 선형 배율 (2^EV)
    float Exposure;
    // 0이면 끔
    float VignetteIntensity;
    // RGB = 색, A = 양
    float4 OverlayColor;
    float2 ViewSize;
    float LetterBoxAspect;
    // 0이면 끔. 1이면 목표 비율만큼 완전히 들어옴.
    float LetterBoxAmount;
};

struct PSInput
{
    float4 Position : SV_POSITION;
};

float3 ACESFitted(float3 color)
{
    const float3x3 ACESInputMat =
    {
        { 0.59719, 0.35458, 0.04823 },
        { 0.07600, 0.87577, 0.04823 },
        { 0.02840, 0.13383, 0.83777 }
    };
    const float3x3 ACESOutputMat =
    {
        { 1.60475, -0.53108, -0.07367 },
        { -0.10208, 1.10813, -0.00605 },
        { -0.00327, -0.07276, 1.07602 }
    };
    float3 v = mul(ACESInputMat, color);

    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    float3 c = a / b;
    return saturate(mul(ACESOutputMat, c));
}

// 정점 버퍼 없이 화면을 덮는 삼각형 하나를 만든다.
PSInput mainVS(uint VertexID : SV_VertexID)
{
    PSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2); // (0,0) (2,0) (0,2)
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

// sRGB 표준 전달 함수 (IEC 61966-2-1). Math/Color.h의 LinearToSRGB와 같은 식이다.
float3 LinearToSRGB(float3 L)
{
    float3 Low = L * 12.92f;
    float3 High = 1.055f * pow(L, 1.0f / 2.4f) - 0.055f;
    return lerp(High, Low, step(L, 0.0031308f));
}

float4 mainPS(PSInput Input) : SV_TARGET
{
    // 입력과 출력 크기가 같으므로 샘플러 없이 같은 픽셀을 그대로 읽는다.
    float3 Color = SceneColor.Load(int3(Input.Position.xy, 0)).rgb;
    float2 UV = Input.Position.xy / ViewSize;

    // 1. Vignette: 렌즈 가장자리로 갈수록 들어오는 빛이 줄어드는 현상
    // Linear에서 곱한다 (cos^4 법칙 근사)
    if (VignetteIntensity > 0.0f)
    {
        float2 D = (UV - 0.5f) * float2(ViewSize.x / ViewSize.y, 1.0f);   // 화면 비율 보정해 원형으로
        float Falloff = 1.0f / (1.0f + dot(D, D) * VignetteIntensity * 4.0f);   // dot(D, D) = R^2
        Color *= Falloff * Falloff;     // cos^4
    }

    if (bToneMap)
        Color = ACESFitted(Color * Exposure);
    if (bEncode)
        Color = LinearToSRGB(Color);

    // 2. Fade: sRGB 에서 섞어야 사람 눈에 적절해 보임.
    // Tone Mapping 이후에 섞어야 고른 색이 잘 반영 됨.
    Color = lerp(Color, OverlayColor.rgb, OverlayColor.a);

    // 3. LetterBox: 검은 띠는 다른 효과가 적용되지 않도록 마지막에 반영
    if (LetterBoxAmount > 0.0f && LetterBoxAspect > 0.0f )
    {
        float Bar = saturate((1.0f - (ViewSize.x / ViewSize.y) / LetterBoxAspect) * 0.5f) * LetterBoxAmount;   // 위(아래) 띠 높이 비율
        if (UV.y < Bar || UV.y > 1.0f - Bar)
            Color = 0.0f;
    }

    // 뷰포트 텍스처를 ImGui가 알파 블렌딩으로 그림.
    return float4(Color, 1.0f);
}
