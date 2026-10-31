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

float4 ShadeDecal(float3 WorldPos)
{
    float3 Local = mul(float4(WorldPos, 1), WorldToDecal).xyz;
    if (any(abs(Local) > 1))
        discard;
    float2 UV = Local.yz * float2(0.5, -0.5) + 0.5;
    float4 Color = DecalTex.Sample(DecalSampleState, UV) * DecalColor;
    Color.a *= FadeAlpha;
    return Color;
}