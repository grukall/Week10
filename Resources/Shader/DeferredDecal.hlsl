#include "DecalCommon.hlsli"

Texture2D<float> SceneDepth : register(t1);

float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    float4 WorldPos = mul(float4(Position, 1), DecalToWorld);
    return mul(WorldPos, CameraViewProjection);
}

float4 mainPS(float4 SvPos : SV_Position) : SV_Target
{
    float2 ScreenUV = SvPos.xy / ViewportSize;
    float Depth = SceneDepth.Load(int3(SvPos.xy, 0));
    float4 NDC = float4(ScreenUV.x * 2 - 1, 1 - ScreenUV.y * 2, Depth, 1);
    float4 World = mul(NDC, InvViewProj);
    World /= World.w;
    
    return ShadeDecal(World.xyz);
}