#pragma once

#include "GameFramework/Actor/DecalActor.h"

// 실제 라이트 없이 스포트라이트 빛 웅덩이를 흉내 내는 데칼 액터.
// Emissive 데칼이라 조명이 끝난 씬 컬러에 빛을 더하며, 그림자와 조명 계산 비용이 없다.
// 빛 색과 세기는 DecalComponent의 DecalColor로 정한다. 1보다 크게 주면 더 밝아진다.
class AFakeSpotLightActor : public ADecalActor
{
	DECLARE_CLASS(AFakeSpotLightActor, ADecalActor)
	REFLECT_START(AFakeSpotLightActor)
	REFLECT_END()

public:
	AFakeSpotLightActor();
};
