#pragma pack_matrix(row_major)

// 기준 단위 -> NDC
cbuffer HUDTransform : register(b0)
{
    matrix PixelToNDC;
};

Texture2D HUDTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(float4(input.position, 1.0f), PixelToNDC);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

// sRGB 표준 전달 함수
float3 LinearToSRGB(float3 L)
{
    float3 Low = L * 12.92f;
    float3 High = 1.055f * pow(L, 1.0f / 2.4f) - 0.055f;
    return lerp(Low, High, step(0.0031308f, L));
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // 표시용 타깃은 sRGB 값을 담으므로 다시 인코딩
    float4 Texel = HUDTexture.Sample(LinearSampler, input.uv);
    float3 Color = LinearToSRGB(saturate(Texel.rgb));

    // 정점 색은 화면 색 그대로 곱하는 틴트 (기본 흰색 = 원래 색)
    return float4(Color * input.color.rgb, Texel.a * input.color.a);
}