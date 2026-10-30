#include "EnginePCH.h"
#include "Engine/HUD.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Component/ActorComponent.h"
#include "UObject/Casts.h"
#include "Asset/AssetManager.h"
#include "Text/Font.h"
#include "Core/ResourcePaths.h"
#include <algorithm>

UFont* FHUD::GetFont()
{
	// 보통은 에셋 스캔 때 이미 읽혀 있다. 없으면 한 번만 직접 읽어 보고, 실패하면 매 프레임 다시 읽지 않는다
	if (UFont* Font = UAssetManager::GetAssetByPath<UFont>(FontPath))
		return Font;

	static bool bTriedLoad = false;
	if (!bTriedLoad)
	{
		bTriedLoad = true;
		if (UFont* Font = UAssetManager::Get().LoadFontAtlas(FontPath, FontAtlasPath))
			return Font;
		HTR_LOG(Warning, "[HUD] font not found: {} (using default font)", FontPath);
	}
	return UAssetManager::GetAssetByPath<UFont>(ResourcePaths::DefaultFont);
}

FHUD* FindHUDForObject(UObject* Object)
{
	AActor* Actor = Cast<AActor>(Object);
	if (!Actor)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(Object))
			Actor = Component->GetOwner();
	}

	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	// Editor 에서는 HUD를 그리지 않는다
	if (!World || !World->IsGameWorld())
		return nullptr;

	return &World->GetHUD();
}

// Color는 틴트. 흰색이면 텍스처 원래 색
void FHUD::AddImage(UTexture2D* Texture, const FVector2& Position, const FVector2& Size, const FLinearColor& Color,
	const FVector2& UVMin, const FVector2& UVMax)
{
    if (!Texture)
        return;

    FHUDDrawCommand Command;
    Command.Type = EHUDCommandType::Image;
    Command.Position = Position;
    Command.Size = Size;
    Command.Color = Color;
    Command.Texture = Texture;
	Command.UVMin = UVMin;
	Command.UVMax = UVMax;
    Commands.Add(std::move(Command));
}

FHUDButtonState FHUD::GetButtonState(const FVector2& Position, const FVector2& Size) const
{
    FHUDButtonState State;
    State.bHovered = Input.bEnabled
        && Input.MousePosition.X >= Position.X && Input.MousePosition.X < Position.X + Size.X
        && Input.MousePosition.Y >= Position.Y && Input.MousePosition.Y < Position.Y + Size.Y;
    State.bHeld = State.bHovered && Input.bMouseDown;
    State.bClicked = State.bHovered && Input.bMousePressed;
    return State;
}

namespace
{
    // Todo : 정렬 기능 추가...
    // 정렬 없이 왼쪽 여백 + 세로 가운데 근사로 버튼 라벨 위치를 정한다
    FVector2 GetButtonLabelPosition(const FVector2& Position, const FVector2& Size, float FontSize)
    {
        constexpr float Padding = 12.0f;
        return FVector2(Position.X + Padding, Position.Y + (Size.Y - FontSize) * 0.5f);
    }
}

bool FHUD::Button(const FString& Label, const FVector2& Position, const FVector2& Size, float FontSize)
{
    const FHUDButtonState State = GetButtonState(Position, Size);

    // 상태별 배경색: Default / Hover / Pressed
    const FLinearColor Background =
        State.bHeld    ? FLinearColor(0.12f, 0.12f, 0.12f, 0.9f) :
        State.bHovered ? FLinearColor(0.38f, 0.38f, 0.38f, 0.9f) :
                         FLinearColor(0.25f, 0.25f, 0.25f, 0.9f);
    AddRect(Position, Size, Background);

    AddText(Label, GetButtonLabelPosition(Position, Size, FontSize), FontSize, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

    return State.bClicked;
}

bool FHUD::ImageButton(UTexture2D* Texture, const FVector2& Position, const FVector2& Size,
    const FString& Label, float FontSize, const FVector2& HitMin, const FVector2& HitMax)
{
    // 판정은 이미지 안의 HitMin~HitMax 비율 영역만
    const FVector2 HitPosition(Position.X + Size.X * HitMin.X, Position.Y + Size.Y * HitMin.Y);
    const FVector2 HitSize(Size.X * (HitMax.X - HitMin.X), Size.Y * (HitMax.Y - HitMin.Y));
    const FHUDButtonState State = GetButtonState(HitPosition, HitSize);

    // 이미지 셰이더가 텍스처 색 × 틴트라서 틴트 밝기로 상태를 표현한다. 1을 넘는 값은 표시용 타깃에서 잘린다.
    const float Brightness = State.bHeld ? 0.7f : (State.bHovered ? 1.2f : 1.0f);
    AddImage(Texture, Position, Size, FLinearColor(Brightness, Brightness, Brightness, 1.0f));

    if (!Label.empty())
        AddText(Label, GetButtonLabelPosition(Position, Size, FontSize), FontSize, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

    // 텍스처가 없어도(경로 오류) 판정은 동작한다
    return State.bClicked;
}

float FHUD::ImageSlider(const FString& Id, UTexture2D* Track, UTexture2D* Handle,
    const FVector2& Position, const FVector2& Size, const FVector2& HandleSize,
    float Value, float MinU, float MaxU)
{
    Value = std::clamp(Value, 0.0f, 1.0f);

    // 손잡이 가운데가 움직이는 가로 구간
    const float StartX = Position.X + Size.X * MinU;
    const float RangeX = Size.X * (MaxU - MinU);
    const float CenterY = Position.Y + Size.Y * 0.5f;

    // 판정: 트랙 폭 전체, 세로는 트랙과 손잡이 중 더 큰 쪽. 빈 곳을 눌러도 그 자리로 바로 옮긴다
    const float HitTop = std::min(Position.Y, CenterY - HandleSize.Y * 0.5f);
    const float HitBottom = std::max(Position.Y + Size.Y, CenterY + HandleSize.Y * 0.5f);
    const FHUDButtonState State = GetButtonState(FVector2(Position.X, HitTop), FVector2(Size.X, HitBottom - HitTop));
    if (State.bClicked)
        ActiveSliderId = Id;

    // 잡은 뒤에는 마우스가 판정 영역 밖으로 나가도 따라간다. 놓는 처리는 SetInput에서 한다
    const bool bDragging = !Id.empty() && ActiveSliderId == Id && Input.bEnabled && Input.bMouseDown;
    if (bDragging && RangeX > 0.0f)
        Value = std::clamp((Input.MousePosition.X - StartX) / RangeX, 0.0f, 1.0f);

    AddImage(Track, Position, Size, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

    // 버튼처럼 Hover면 밝게, 끄는 중이면 어둡게
    const float Brightness = bDragging ? 0.8f : (State.bHovered ? 1.2f : 1.0f);
    const float HandleX = StartX + RangeX * Value;
    AddImage(Handle, FVector2(HandleX - HandleSize.X * 0.5f, CenterY - HandleSize.Y * 0.5f), HandleSize,
        FLinearColor(Brightness, Brightness, Brightness, 1.0f));

    return Value;
}
