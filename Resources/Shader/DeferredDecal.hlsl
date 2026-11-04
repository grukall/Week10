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

float2 WorldToDecalUV(float3 World)
{
    return DecalLocalToUV(WorldToDecalLocal(World));
}

FDecalOutput mainPS(float4 SvPos : SV_Position)
{
    int2 Pixel = int2(SvPos.xy);
    float Depth = SceneDepth.Load(int3(Pixel, 0));
    float3 World = ReconstructWorld(Pixel, Depth);
    float2 UV = WorldToDecalUV(World);

    // 하드웨어 ddx, ddy는 2x2 쿼드 안의 이웃을 고르지 못해 물체 경계에서 다른 표면의 UV와 빼진다.
    // 그러면 미분이 튀어 가장 작은 밉(평균색)을 읽고 경계에 점이 생긴다.
    // 그래서 좌우, 위아래 이웃 중 깊이가 더 비슷한 쪽(같은 표면)을 골라 미분을 직접 구한다.
    float DepthL = SceneDepth.Load(int3(Pixel + int2(-1, 0), 0));
    float DepthR = SceneDepth.Load(int3(Pixel + int2(1, 0), 0));
    float DepthU = SceneDepth.Load(int3(Pixel + int2(0, -1), 0));
    float DepthD = SceneDepth.Load(int3(Pixel + int2(0, 1), 0));

    // 고른 이웃의 월드 위치로 오른쪽(+x), 아래쪽(+y) 한 픽셀 차이를 만든다. UV 미분과 표면 노멀에 같이 쓴다
    bool bRight = abs(DepthR - Depth) < abs(DepthL - Depth);
    bool bDown = abs(DepthD - Depth) < abs(DepthU - Depth);
    float3 WorldX = bRight ? ReconstructWorld(Pixel + int2(1, 0), DepthR) : ReconstructWorld(Pixel + int2(-1, 0), DepthL);
    float3 WorldY = bDown ? ReconstructWorld(Pixel + int2(0, 1), DepthD) : ReconstructWorld(Pixel + int2(0, -1), DepthU);
    float3 DPDX = bRight ? WorldX - World : World - WorldX;
    float3 DPDY = bDown ? WorldY - World : World - WorldY;
    float2 DX = bRight ? WorldToDecalUV(WorldX) - UV : UV - WorldToDecalUV(WorldX);
    float2 DY = bDown ? WorldToDecalUV(WorldY) - UV : UV - WorldToDecalUV(WorldY);

    // Deferred는 받는 메시의 정점 노멀이 없어 표면 위 두 방향의 외적으로 노멀을 만든다(면 노멀에 가깝다).
    // 외적 순서로는 앞뒤를 알 수 없으니 카메라 쪽(같은 픽셀의 near 평면 점)을 향하게 뒤집는다
    float3 SurfaceNormal = normalize(cross(DPDY, DPDX));
    float3 ToCamera = ReconstructWorld(Pixel, 0.0f) - World;
    if (dot(SurfaceNormal, ToCamera) < 0.0f)
        SurfaceNormal = -SurfaceNormal;

    return ShadeDecalGrad(World, DX, DY, SurfaceNormal);
}
