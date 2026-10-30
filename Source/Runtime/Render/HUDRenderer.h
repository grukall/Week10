#pragma once
#include "Render/PipelineState.h"
#include "Render/Buffer.h"
#include "Render/Vertex.h"

class FHUD;
class UFont;
class UTexture2D;
enum class EHUDCommandType : uint8;
struct FHUDDrawCommand;
struct FSceneView;

// FHUD의 그리기 명령을 화면 픽셀 좌표로 그린다. 렌더 패스는 호출하는 쪽이 연다.
// 같은 종류의 명령이 연속된 구간을 한 번에 그려서, 명령 순서(겹치는 순서)를 지킨다.
class FHUDRenderer
{
public:
	FHUDRenderer() = default;
	~FHUDRenderer() = default;
	FHUDRenderer(const FHUDRenderer&) = delete;
	FHUDRenderer& operator=(const FHUDRenderer&) = delete;

	bool Init();
	// 바인딩된 렌더 타깃(ScreenWidth x ScreenHeight)에 HUD를 그린다.
	// View는 이 타깃에 그려진 장면의 View. 월드 텍스트를 투영하는 데 쓰며, 없으면 월드 텍스트를 건너뛴다.
	void OnRender(const FHUD& HUD, float ScreenWidth, float ScreenHeight, const FSceneView* View = nullptr);

private:
	void AppendRect(const FHUDDrawCommand& Command);
	void AppendText(const FHUDDrawCommand& Command);
	// 월드 텍스트의 왼쪽 위(기준 단위)를 구한다. 카메라 뒤거나 View가 없으면 false
	bool ResolveWorldTextOrigin(const FHUDDrawCommand& Command, FVector2& OutOrigin) const;
	float MeasureTextWidth(const FString& Text, float FontSize) const;
	void AppendImage(const FHUDDrawCommand& Command);
	void AddQuad(float Left, float Top, float Right, float Bottom, const FVector2& UVMin, const FVector2& UVMax, const FVector4& Color);
	// 지금까지 쌓은 정점을 Type에 맞는 파이프라인으로 그리고 비운다
    void Flush(EHUDCommandType Type, UTexture2D* Texture);


	static constexpr uint32 MaxQuads = 4096;

	FPipelineState RectPipeline;
	FPipelineState TextPipeline;
    FPipelineState ImagePipeline;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FConstantBuffer> TransformBuffer;     // b0:  PixelToNDC
	TUniquePtr<FConstantBuffer> MSDFParamsBuffer;    // b1:  PxRange
	TArray<FVertex> Vertices;
    UFont* Font = nullptr;                           // 에셋 매니저 소유

	// OnRender 동안만 유효한 값. 월드 텍스트 투영에 쓴다.
	const FSceneView* CurrentView = nullptr;
	float CurrentScreenWidth = 0.0f;
	float CurrentScreenHeight = 0.0f;
	float CurrentScale = 1.0f;
};