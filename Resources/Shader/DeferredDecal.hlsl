#include "DecalCommon.hlsli"

Texture2D<float> SceneDepth : register(t1);

float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    float4 WorldPos = mul(float4(Position, 1), DecalToWorld);
    return mul(WorldPos, CameraViewProjection);
}

// 픽셀 좌표와 깊이로 월드 위치를 복원한다.
float3 ReconstructWorld(int2 Pixel, float Depth)
{
    float2 ScreenUV = (Pixel + 0.5) / ViewportSize;
    float4 NDC = float4(ScreenUV.x * 2 - 1, 1 - ScreenUV.y * 2, Depth, 1);
    float4 World = mul(NDC, InvViewProj);
    return World.xyz / World.w;
}

float2 PixelDecalUV(int2 Pixel, float Depth)
{
    return DecalLocalToUV(WorldToDecalLocal(ReconstructWorld(Pixel, Depth)));
}

float4 mainPS(float4 SvPos : SV_Position) : SV_Target
{
    int2 Pixel = int2(SvPos.xy);
    float Depth = SceneDepth.Load(int3(Pixel, 0));
    float2 UV = PixelDecalUV(Pixel, Depth);

    // 하드웨어 ddx, ddy는 2x2 쿼드 안의 이웃을 고르지 못해 물체 경계에서 다른 표면의 UV와 빼진다.
    // 그러면 미분이 튀어 가장 작은 밉(평균색)을 읽고 경계에 점이 생긴다.
    // 그래서 좌우, 위아래 이웃 중 깊이가 더 비슷한 쪽(같은 표면)을 골라 미분을 직접 구한다.
    float DepthL = SceneDepth.Load(int3(Pixel + int2(-1, 0), 0));
    float DepthR = SceneDepth.Load(int3(Pixel + int2(1, 0), 0));
    float DepthU = SceneDepth.Load(int3(Pixel + int2(0, -1), 0));
    float DepthD = SceneDepth.Load(int3(Pixel + int2(0, 1), 0));

    float2 DX = abs(DepthR - Depth) < abs(DepthL - Depth)
        ? PixelDecalUV(Pixel + int2(1, 0), DepthR) - UV
        : UV - PixelDecalUV(Pixel + int2(-1, 0), DepthL);
    float2 DY = abs(DepthD - Depth) < abs(DepthU - Depth)
        ? PixelDecalUV(Pixel + int2(0, 1), DepthD) - UV
        : UV - PixelDecalUV(Pixel + int2(0, -1), DepthU);

    return ShadeDecalGrad(ReconstructWorld(Pixel, Depth), DX, DY);
}
