#pragma once

enum class ERasterizerState : uint8 { SolidBack, SolidNone, SolidFront, Wireframe, Count };
enum class EDepthStencilState : uint8
{
	Default, ReadOnly, Disabled, StencilMask, StencilOutline, ReadOnlyLessEqual,
	// 물체를 그릴 때 데칼 수신 비트를 기록한다. 깊이는 Default(Less, 쓰기). 참조값: 받으면 0, 안 받으면 DecalStencilBit
	DecalReceiverWrite,
	// Deferred 방식 박스. 뒷면이 벽 속에 있어도 그려지도록 깊이 테스트를 끄고, 수신 비트가 0인 픽셀만 통과한다(참조값 0)
	DeferredDecalTest,
	// Mesh 방식 다시 그리기. Prepass와 같은 깊이를 통과시키려고 LessEqual 읽기 전용, 수신 비트가 0인 픽셀만 통과한다(참조값 0)
	DecalMeshTest,
	Count
};

// 스텐실 8비트 중 데칼 수신 여부에 쓰는 비트. 선택 아웃라인은 나머지 하위 비트만 쓴다(UE의 RECEIVE_DECAL 비트와 같은 방식).
constexpr uint8 DecalStencilBit = 0x80;
constexpr uint8 OutlineStencilMask = static_cast<uint8>(~DecalStencilBit);
enum class EBlendState : uint8 { Opaque, AlphaBlend, NoColorWrite, Emissive, Count };
enum class ESamplerState : uint8 { LinearClamp, LinearWrap, PointClamp, ShadowComparison, Count };
