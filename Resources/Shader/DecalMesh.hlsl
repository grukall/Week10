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

cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix; // World의 역전치
    uint bReceiveShadow;
    uint3 ObjectPadding;
};

PSInput mainVS(float3 Position : POSITION)
{ 
    PSInput Input;
    float4 WorldPos = mul(float4(Position, 1), World);
    Input.Position = mul(WorldPos, ViewProjection);
    Input.WorldPos = WorldPos.xyz;
    return Input;
}

//Rasterizer
//월드 좌표를 TEXCOORD0에 실어 보내면 래스터라이저가 삼각형 안에서 보간해서 픽셀마다 정확한 월드 위치를 넘겨준다.

float4 mainPS(PSInput Input) : SV_Target
{
    return ShadeDecal(Input.WorldPos);
}