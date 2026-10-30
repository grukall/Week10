#pragma once
#include "Render/PipelineState.h"
#include "Render/Buffer.h"
#include "Math/Color.h"
#include "Math/Vector2.h"
class FTexture2D;

struct FGammaEncodeConstants
{
	uint32 bEncode;
	uint32 bToneMap;
	float Exposure;
	float VignetteIntensity;   		// 0이면 끔
	FLinearColor OverlayColor;	 	// Fade
	FVector2 ViewSize;				// Pixel
	float LetterBoxAspect;    		
	float LetterBoxAmout;			// 0이면 끔
};

static_assert(sizeof(FGammaEncodeConstants) == 48, "Check the size");

// 파이프라인이 Show Flag를 반영해 채우는 최종 값
struct FGammaEncodeParams
{
	bool bEncode = true;
	bool bToneMap = true;
	float ExposureEV = 0.0f;
	float VignetteIntensity = 0.0f;
	FLinearColor OverlayColor = FLinearColor(0, 0, 0, 0);
	float LetterBoxAspect = 0.0f;
	float LetterBoxAmount = 0.0f;
};

// Linear 씬 컬러를 ACES로 톤매핑하고 화면 표시용 sRGB로 인코딩하는 후처리. 언리얼의 톤매퍼 패스에 해당한다.
class FGammaEncodeRenderer
{
public:
	FGammaEncodeRenderer() = default;
	~FGammaEncodeRenderer() = default;

	bool Init();
	
	// 현재 바인딩된 렌더 타깃(ViewWidth x ViewHeight)에 SceneColor를 그린다. 렌더 패스는 호출하는 쪽이 연다.	
	void OnRender(FTexture2D* SceneColor, const FGammaEncodeParams& Params, float ViewWidth, float ViewHeight);

	FGammaEncodeRenderer(const FGammaEncodeRenderer&) = delete;
	FGammaEncodeRenderer& operator=(const FGammaEncodeRenderer&) = delete;

private:
	FShaderProgram* Shader = nullptr;
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
};
