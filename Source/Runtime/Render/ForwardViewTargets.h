#pragma once

#include "Render/RenderingInfo.h"
#include "Render/TileLightCullingResources.h"

// 기존 Forward 경로의 linear 씬 컬러와 수동 인코딩 표시 컬러를 한 뷰가 소유한다.
struct FForwardViewTargets
{
	TUniquePtr<FTexture2D> SceneColorTarget;
	TUniquePtr<FTexture2D> DBufferTarget;
	// 데칼 노멀(언리얼 DBufferB). RGB = 덮인 만큼 곱해진 월드 노멀, A = 덮인 정도
	TUniquePtr<FTexture2D> DBufferNormalTarget;
	TUniquePtr<FTexture2D> ColorTarget;
	TUniquePtr<FTexture2D> DepthTarget;
	TUniquePtr<FTexture2D> FxaaTarget;
	FRenderingInfo SceneRenderingInfo{};
	FRenderingInfo RenderingInfo{};
	FTileLightCullingResources LightCulling;
	uint32 Width = 0;
	uint32 Height = 0;

	void Resize(uint32 InWidth, uint32 InHeight);
	void Reset();
	static void BuildViewRenderingInfos(FRenderingInfo& OutSceneInfo, FRenderingInfo& OutViewInfo, FTexture2D* SceneColor, FTexture2D* ViewColor, FTexture2D* Depth, uint32 Width, uint32 Height);
};
