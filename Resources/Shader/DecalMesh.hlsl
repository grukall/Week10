#include "DecalCommon.hlsli"

// C++ FDecalMeshVertex와 순서·크기가 같아야 한다. 입력 레이아웃은 이 구조체에서 리플렉션으로 만들어진다.
struct VSInput
{
    float3 Position : POSITION;     // 월드 위치
    float3 Normal : NORMAL;         // 받는 메시의 정점 노멀(월드)
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : NORMAL;
};

cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraPosition;
    float ViewPadding;
};

// 카메라까지 거리에 곱하는 깊이 바이어스 비율이다. 멀수록 깊이 정밀도가 떨어지니 거리에 비례해 당긴다.
static const float DecalDepthBias = 0.001f;

PSInput mainVS(VSInput Input)
{
    PSInput Output;
    // 받는 물체와 같은 면이라 z-fighting이 난다. 시선 방향으로 카메라 쪽에 당기면 화면 위치는 그대로고 깊이만 앞으로 온다.
    float3 ToCamera = CameraPosition - Input.Position;
    float3 BiasedPos = Input.Position + ToCamera * DecalDepthBias;
    Output.Position = mul(float4(BiasedPos, 1), ViewProjection);
    // UV는 당기기 전 위치로 계산해야 무늬가 밀리지 않는다
    Output.WorldPos = Input.Position;
    Output.Normal = Input.Normal;
    return Output;
}

//Rasterizer
//월드 좌표와 노멀을 넘기면 래스터라이저가 삼각형 안에서 보간해서 픽셀마다 넘겨준다.
//보간된 노멀은 길이가 1보다 짧아지므로 DecalNormalToWorld에서 다시 normalize한다.
FDecalOutput mainPS(PSInput Input)
{
    return ShadeDecal(Input.WorldPos, Input.Normal);
}
