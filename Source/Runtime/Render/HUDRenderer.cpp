#include "EnginePCH.h"
#include "Render/HUDRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
#include "Engine/HUD.h"
#include "Text/Font.h"
#include "Text/TextRenderer.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"
#include "Render/SceneView.h"

namespace
{
	// Text Shader b1
	struct FHUDMSDFParams
	{
		float PxRange = 2.0f;
		float Padding[3] = {};
	};

	// 반투명 UI : Culling off, Alpha Blend, Depth off (항상 맨 위)
	void SetupHUDPipeline(FPipelineState& Pipeline, const char* ShaderPath)
	{
		Pipeline.Shader = FRenderResourceManager::GetShaderProgram(ShaderPath);
		Pipeline.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		Pipeline.RasterizerState = ERasterizerState::SolidNone;
		Pipeline.BlendState = EBlendState::AlphaBlend;
		Pipeline.DepthStencilState = EDepthStencilState::Disabled;
	}

	FVector4 ToVector4(const FLinearColor& Color)
	{
		return FVector4(Color.R, Color.G, Color.B, Color.A);
	}
}

bool FHUDRenderer::Init()
{
	// 사각형
	SetupHUDPipeline(RectPipeline, "Resources/Shader/BoundingBoxShader.hlsl");
	// 텍스트
	SetupHUDPipeline(TextPipeline, "Resources/Shader/HUDTextShader.hlsl");
    // 이미지
	SetupHUDPipeline(ImagePipeline, "Resources/Shader/HUDImageShader.hlsl");

	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FVertex) * MaxQuads * 6, sizeof(FVertex));
	TransformBuffer = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
	MSDFParamsBuffer = RenderCommand::CreateConstantBuffer(sizeof(FHUDMSDFParams));


	return RectPipeline.Shader && TextPipeline.Shader && ImagePipeline.Shader;
}

void FHUDRenderer::AddQuad(float Left, float Top, float Right, float Bottom,
	const FVector2& UVMin, const FVector2& UVMax, const FVector4& Color)
{
	if (static_cast<uint32>(Vertices.Num()) + 6 > MaxQuads * 6)
		return;   // 넘치면 버린다

	// 왼쪽 위 오른쪽 위 → 오른쪽 아래 / 왼쪽 위 → 오른쪽 아래 → 왼쪽 아래
	Vertices.Add({ FVector(Left,  Top,    0.0f), FVector2(UVMin.X, UVMin.Y), Color });  // 좌상
	Vertices.Add({ FVector(Right, Top,    0.0f), FVector2(UVMax.X, UVMin.Y), Color });  // 우상
	Vertices.Add({ FVector(Right, Bottom, 0.0f), FVector2(UVMax.X, UVMax.Y), Color });  // 우하

	Vertices.Add({ FVector(Left,  Top,    0.0f), FVector2(UVMin.X, UVMin.Y), Color });  // 좌상
	Vertices.Add({ FVector(Right, Bottom, 0.0f), FVector2(UVMax.X, UVMax.Y), Color });  // 우하
	Vertices.Add({ FVector(Left,  Bottom, 0.0f), FVector2(UVMin.X, UVMax.Y), Color });  // 좌하
}

void FHUDRenderer::AppendRect(const FHUDDrawCommand& Command)
{
	AddQuad(Command.Position.X, Command.Position.Y,
		Command.Position.X + Command.Size.X, Command.Position.Y + Command.Size.Y,
		FVector2(), FVector2(), ToVector4(Command.Color));
}

void FHUDRenderer::AppendText(const FHUDDrawCommand& Command)
{
	if (!Font || !Font->AtlasTexture || Command.Text.empty())
		return;

	FVector2 Origin = Command.Position;
	if (Command.bWorldSpace && !ResolveWorldTextOrigin(Command, Origin))
		return;

	// 글리프 좌표는 위로 + 픽셀은 아래로 +
    // 기준선을 FontSize만큼 아래에 둔다.
	const float Size = Command.FontSize;
	const float Baseline = Origin.Y + Size;
	float PenX = Origin.X;
	const FVector4 Color = ToVector4(Command.Color);

	for (uint32 CodePoint : DecodeUTF8(Command.Text))
	{
		const FGlyphInfo* Glyph = Font->GlyphMap.Find(CodePoint);
		if (!Glyph)	continue;    // 아틀라스에 없는 글자는 건너뛴다
		if (Glyph->bHasBounds)   // 공백은 그릴 것 없이 펜만 옮긴다
		{
			const float Left   = PenX + Glyph->PlaneLeft * Size;
			const float Right  = PenX + Glyph->PlaneRight * Size;
			const float Top    = Baseline - Glyph->PlaneTop * Size;
			const float Bottom = Baseline - Glyph->PlaneBottom * Size;

			// 아틀라스 픽셀 좌표 -> UV. 아틀라스는 아래가 0이라 V를 뒤집음.
			const FVector2 UVMin(Glyph->AtlasLeft / Font->Width, 1.0f - Glyph->AtlasTop / Font->Height);
			const FVector2 UVMax(Glyph->AtlasRight / Font->Width, 1.0f - Glyph->AtlasBottom / Font->Height);

			AddQuad(Left, Top, Right, Bottom, UVMin, UVMax, Color);
		}
		PenX += Glyph->Advance * Size;
	}
}

// 장면을 그린 View 그대로 투영해야 글자가 대상에 붙어 보인다. 카메라 컴포넌트의 행렬은
// FGameViewportClient::CreateSceneView가 만드는 View(Roll 제거, 뷰포트 화면비)와 달라서 쓰지 않는다.
bool FHUDRenderer::ResolveWorldTextOrigin(const FHUDDrawCommand& Command, FVector2& OutOrigin) const
{
	if (!CurrentView)
		return false;

	const FVector& P = Command.WorldPosition;
	const FVector4 Clip = FVector4(P.X, P.Y, P.Z, 1.0f) * CurrentView->ViewProjection;
	if (Clip.W <= 1.0e-4f)
		return false;   // 카메라 뒤

	// NDC -> 타깃 픽셀 -> 기준 단위 (OnRender의 PixelToNDC가 다시 Scale을 곱한다)
	const float NdcX = Clip.X / Clip.W;
	const float NdcY = Clip.Y / Clip.W;
	const float CenterX = (NdcX * 0.5f + 0.5f) * CurrentScreenWidth / CurrentScale;
	const float CenterY = (0.5f - NdcY * 0.5f) * CurrentScreenHeight / CurrentScale;

	// 투영된 점에 글자 중심을 맞춘다
	OutOrigin.X = CenterX - MeasureTextWidth(Command.Text, Command.FontSize) * 0.5f + Command.ScreenOffset.X;
	OutOrigin.Y = CenterY - Command.FontSize * 0.5f + Command.ScreenOffset.Y;
	return true;
}

float FHUDRenderer::MeasureTextWidth(const FString& Text, float FontSize) const
{
	float Width = 0.0f;
	for (uint32 CodePoint : DecodeUTF8(Text))
	{
		if (const FGlyphInfo* Glyph = Font->GlyphMap.Find(CodePoint))
			Width += Glyph->Advance * FontSize;
	}
	return Width;
}

void FHUDRenderer::AppendImage(const FHUDDrawCommand& Command)
{
	// D3D 텍스처는 왼쪽 위가 UV (0,0)이라 화면 좌표와 방향이 같다
	AddQuad(Command.Position.X, Command.Position.Y,
		Command.Position.X + Command.Size.X, Command.Position.Y + Command.Size.Y,
		Command.UVMin, Command.UVMax, ToVector4(Command.Color));
}

void FHUDRenderer::Flush(EHUDCommandType Type, UTexture2D* Texture)
{
	if (Vertices.Num() == 0)
		return;

	RenderCommand::UpdateBufferData(VertexBuffer.get(), Vertices.GetData(), sizeof(FVertex) * Vertices.Num());

	if (Type == EHUDCommandType::Text)
	{
		FHUDMSDFParams Params;
		Params.PxRange = Font->DistanceRange;
		RenderCommand::UpdateBufferData(MSDFParamsBuffer.get(), &Params, sizeof(Params));

		RenderCommand::BindPipelineState(TextPipeline);
		RenderCommand::BindShaderResource(0, Font->AtlasTexture, EShaderBindFlagBits::Pixel);
		RenderCommand::BindConstantBuffer(1, MSDFParamsBuffer.get(), EShaderBindFlagBits::Pixel);
	}
	else if (Type == EHUDCommandType::Image)
	{
		RenderCommand::BindPipelineState(ImagePipeline);
		RenderCommand::BindShaderResource(0, Texture, EShaderBindFlagBits::Pixel);
	}
	else
	{
		RenderCommand::BindPipelineState(RectPipeline);
	}

	RenderCommand::BindConstantBuffer(0, TransformBuffer.get(), EShaderBindFlagBits::Vertex);
	RenderCommand::BindVertexBuffer(VertexBuffer.get());
	RenderCommand::Draw(Vertices.Num(), 0);

	Vertices.Reset();
}

void FHUDRenderer::OnRender(const FHUD& HUD, float ScreenWidth, float ScreenHeight, const FSceneView* View)
{
	if (ScreenWidth <= 0.0f || ScreenHeight <= 0.0f || HUD.GetCommands().Num() == 0)
		return;

	// HUD 폰트는 Assets에 있어 Init 때는 아직 스캔 전일 수 있다. 그릴 때마다 가져온다 (캐시 조회라 가볍다)
	Font = FHUD::GetFont();

	// 기준 단위 -> 실제 픽셀(×Scale) -> NDC(-1~1 / 위로 +y)
	const float Scale = HUD.GetScale();
	CurrentView = View;
	CurrentScreenWidth = ScreenWidth;
	CurrentScreenHeight = ScreenHeight;
	CurrentScale = Scale;
	const FMatrix PixelToNDC(
		2.0f * Scale / ScreenWidth, 0.0f,                         0.0f, 0.0f,
		0.0f,                       -2.0f * Scale / ScreenHeight, 0.0f, 0.0f,
		0.0f,                       0.0f,                         1.0f, 0.0f,
		-1.0f,                      1.0f,                         0.0f, 1.0f);

	RenderCommand::UpdateBufferData(TransformBuffer.get(), &PixelToNDC, sizeof(FMatrix));

	// 같은 종류가 이어지는 동안 정점을 쌓고, 종류가 바뀌면 그때까지 쌓은 것을 그린다
	Vertices.Reset();

    EHUDCommandType BatchType = HUD.GetCommands()[0].Type;
	UTexture2D* BatchTexture = HUD.GetCommands()[0].Texture;
	
    for (const FHUDDrawCommand& Command : HUD.GetCommands())
	{
		if (Command.Type != BatchType || Command.Texture != BatchTexture)
		{
			Flush(BatchType, BatchTexture);
			BatchType = Command.Type;
			BatchTexture = Command.Texture;
		}
		switch (Command.Type)
		{
		case EHUDCommandType::Rect:  AppendRect(Command);  break;
		case EHUDCommandType::Text:  AppendText(Command);  break;
		case EHUDCommandType::Image: AppendImage(Command); break;
		}
	}

	Flush(BatchType, BatchTexture);
	CurrentView = nullptr;
}
