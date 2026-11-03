#include "EnginePCH.h"
#include "FakeSpotLightActor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"
#include "Core/ResourcePaths.h"

AFakeSpotLightActor::AFakeSpotLightActor()
{
	// ADecalActor가 만든 데칼 컴포넌트를 빛을 더하는 데칼로 바꾼다
	DecalComponent->BlendType = static_cast<uint32>(EDecalBlendType::Emissive);
	DecalComponent->SetTexture("Assets/Textures/Decal/FakeSpotLight.png");
	// X는 투영 깊이, YZ는 빛 웅덩이 반지름이다. 놓자마자 바닥에 닿도록 깊이를 넉넉히 준다.
	DecalComponent->DecalSize = FVector(4.0f, 2.0f, 2.0f);
	// 따뜻한 할로겐 색. 1보다 커서 어두운 곳에서도 확 밝아진다.
	DecalComponent->DecalColor = FLinearColor(1.6f, 1.3f, 0.9f, 1.0f);
	// 스포트라이트처럼 위에서 아래(로컬 +X가 -Z)로 비춘다
	DecalComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));

	BillboardComponent->SetTexture(ResourcePaths::SpotLightIcon);
}
