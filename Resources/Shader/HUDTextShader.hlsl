#pragma pack_matrix(row_major)

// 픽셀 좌표 -> NDC (사각형과 같은 행렬)
cbuffer HUDTransform : register(b0)
{
    matrix PixelToNDC;
};

// msdf-atlas-gen의 distance range (UFont::DistanceRange)
cbuffer MSDFParams : register(b1)
{
    float PxRange;
    float3 Padding;
};

Texture2D MSDFTexture : register(t0);
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

float Median(float3 msdf)
{
    return max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float3 Sample = MSDFTexture.Sample(LinearSampler, input.uv).rgb;
    float SignedDist = Median(Sample) - 0.5f;

    // 화면에서 실제로 차지하는 픽셀 기준 거리 범위
    float TexWidth, TexHeight;
    MSDFTexture.GetDimensions(TexWidth, TexHeight);
    float2 UnitRange = float2(PxRange, PxRange) / float2(TexWidth, TexHeight);
    float2 ScreenTexSize = float2(1.0f, 1.0f) / fwidth(input.uv);
    float ScreenPxRange = max(0.5f * dot(UnitRange, ScreenTexSize), 1.0f);

    // 글자 안쪽은 1, 바깥은 0, 경계는 부드럽게. clip 대신 알파로 블렌딩
    float Opacity = saturate(ScreenPxRange * SignedDist + 0.5f);
    return float4(input.color.rgb, input.color.a * Opacity);
}