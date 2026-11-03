#include "DecalCommon.hlsli"

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraPosition;
    float ViewPadding;
};

// 카메라까지 거리에 곱하는 깊이 바이어스 비율이다. 멀수록 깊이 정밀도가 떨어지니 거리에 비례해 당긴다.
static const float DecalDepthBias = 0.001f;

PSInput mainVS(float3 Position : POSITION)
{
    PSInput Input;
    // 받는 물체와 같은 면이라 z-fighting이 난다. 시선 방향으로 카메라 쪽에 당기면 화면 위치는 그대로고 깊이만 앞으로 온다.
    float3 ToCamera = CameraPosition - Position;
    float3 BiasedPos = Position + ToCamera * DecalDepthBias;
    Input.Position = mul(float4(BiasedPos, 1), ViewProjection);
    // UV는 당기기 전 위치로 계산해야 무늬가 밀리지 않는다
    Input.WorldPos = Position;
    return Input;
}

//Rasterizer
//월드 좌표를 TEXCOORD0에 실어 보내면 래스터라이저가 삼각형 안에서 보간해서 픽셀마다 정확한 월드 위치를 넘겨준다.
FDecalOutput mainPS(PSInput Input)
{
    return ShadeDecal(Input.WorldPos);
}