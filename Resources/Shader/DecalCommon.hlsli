#pragma pack_matrix(row_major)


cbuffer FDecalConstants : register(b6)
{
    matrix CameraViewProjection;
    matrix InvViewProj;
    matrix DecalToWorld;
    matrix WorldToDecal;
    float4 DecalColor;
    float2 ViewportSize;
    float FadeAlpha;
    float Padding;
};

Texture2D DecalTex : register(t0);
SamplerState DecalSampleState : register(s0);

// 월드 위치를 데칼 박스 로컬 공간(-1 ~ 1)으로 옮긴다.
float3 WorldToDecalLocal(float3 WorldPos)
{
    return mul(float4(WorldPos, 1), WorldToDecal).xyz;
}

// 데칼은 로컬 X로 투영하고 YZ 평면을 텍스처로 쓴다.
float2 DecalLocalToUV(float3 Local)
{
    return Local.yz * float2(0.5, -0.5) + 0.5;
}

// UV 미분을 직접 받아 샘플링한다.
// 미분은 화면 1픽셀당 UV 변화량이고, GPU는 이 값으로 밉 레벨을 고른다.
float4 ShadeDecalGrad(float3 WorldPos, float2 DX, float2 DY)
{
    float3 Local = WorldToDecalLocal(WorldPos);
    if (any(abs(Local) > 1))
        discard;
    float4 Color = DecalTex.SampleGrad(DecalSampleState, DecalLocalToUV(Local), DX, DY) * DecalColor;
    Color.a *= FadeAlpha;
    return Color;
}

// 메시처럼 표면이 이어져 있으면 하드웨어 미분(ddx, ddy)을 그대로 쓴다.
// discard보다 먼저 미분을 구해야 2x2 안의 이웃 값이 온전하다.
float4 ShadeDecal(float3 WorldPos)
{
    float2 UV = DecalLocalToUV(WorldToDecalLocal(WorldPos));
    return ShadeDecalGrad(WorldPos, ddx(UV), ddy(UV));
}
