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
    float NormalMode;   // 0: 노멀맵 없음, 1: 있음, 2: 있고 G 채널을 뒤집는다
};

Texture2D DecalTex : register(t0);
Texture2D DecalNormalTex : register(t2);
SamplerState DecalSampleState : register(s0);

// SV_Target0은 색, SV_Target1은 노멀이다.
// 렌더 타깃이 하나만 꽂힌 패스(Unlit, Emissive)에서는 노멀 출력이 버려진다.
struct FDecalOutput
{
    float4 Color : SV_Target0;
    float4 Normal : SV_Target1;
};

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

// 노멀맵을 데칼의 탄젠트 공간에서 월드로 옮긴다.
// UV의 U는 로컬 +Y, 노멀은 투영 방향의 반대(-X)라 받는 면을 향한다. 바이탄젠트는 UberLit과 같이 cross(N, T)로 만든다.
float3 DecalNormalToWorld(float2 UV, float2 DX, float2 DY)
{
    float3 Normal = -normalize(DecalToWorld[0].xyz);
    float3 Tangent = normalize(DecalToWorld[1].xyz);
    float3 BiTangent = cross(Normal, Tangent);

    float3 TextureNormal = normalize(DecalNormalTex.SampleGrad(DecalSampleState, UV, DX, DY).xyz * 2.0f - 1.0f);
    TextureNormal.y = NormalMode > 1.5f ? -TextureNormal.y : TextureNormal.y;
    return normalize(mul(TextureNormal, float3x3(Tangent, BiTangent, Normal)));
}

// UV 미분을 직접 받아 샘플링한다.
// 미분은 화면 1픽셀당 UV 변화량이고, GPU는 이 값으로 밉 레벨을 고른다.
FDecalOutput ShadeDecalGrad(float3 WorldPos, float2 DX, float2 DY)
{
    float3 Local = WorldToDecalLocal(WorldPos);
    if (any(abs(Local) > 1))
        discard;
    float2 UV = DecalLocalToUV(Local);

    FDecalOutput Output;
    Output.Color = DecalTex.SampleGrad(DecalSampleState, UV, DX, DY) * DecalColor;
    Output.Color.a *= FadeAlpha;

    // 노멀도 색과 같은 알파로 덮는다. 블렌드가 RGB에 알파를 곱해 쌓으므로 노멀맵이 없으면 알파 0으로 아무것도 바꾸지 않는다.
    Output.Normal = float4(0, 0, 0, 0);
    if (NormalMode > 0.5f)
        Output.Normal = float4(DecalNormalToWorld(UV, DX, DY), Output.Color.a);
    return Output;
}

// 메시처럼 표면이 이어져 있으면 하드웨어 미분(ddx, ddy)을 그대로 쓴다.
// discard보다 먼저 미분을 구해야 2x2 안의 이웃 값이 온전하다.
FDecalOutput ShadeDecal(float3 WorldPos)
{
    float2 UV = DecalLocalToUV(WorldToDecalLocal(WorldPos));
    return ShadeDecalGrad(WorldPos, ddx(UV), ddy(UV));
}
