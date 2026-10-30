#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Math/Vector2.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Render/Texture2D.h"

class UObject;
class UFont;

// Rect 위에 Text
enum class EHUDCommandType : uint8
{
	Rect,
	Text,
    Image,
};

struct FHUDDrawCommand
{
	EHUDCommandType Type = EHUDCommandType::Rect;
	FVector2 Position;              // 위치 (Pixel)
	FVector2 Size;                  // 너비, 높이 (Pixel)
	FLinearColor Color;
	FString Text;                   // Text 전용
	float FontSize = 0.0f;          // Text 전용
    UTexture2D* Texture = nullptr;  // Image 전용
	FVector2 UVMin = FVector2(0.0f, 0.0f);
	FVector2 UVMax = FVector2(1.0f, 1.0f);

	// Text 전용. true면 Position 대신 WorldPosition을 렌더 시점에 그 뷰포트의 View로 투영하고,
	// 투영된 점에 글자 중심을 맞춘 뒤 ScreenOffset(기준 단위)만큼 더 옮긴다.
	bool bWorldSpace = false;
	FVector WorldPosition;
	FVector2 ScreenOffset;
};

// GameViewportClient가 매 프레임 채우는 HUD 입력
struct FHUDInput
{
	FVector2 MousePosition;
	bool bMouseDown = false;      // 왼쪽 버튼을 누르고 있는지
	bool bMousePressed = false;   // 이번 프레임에 왼쪽 버튼을 눌렀는지
	bool bEnabled = false;        // 이 화면에 입력 권한이 있는지 (포커스)
};

// 버튼 영역의 이번 프레임 입력 상태. 그리기와 별개로 판정만 한다.
struct FHUDButtonState
{
	bool bHovered = false;   // 마우스가 버튼 위에 있음
	bool bHeld = false;      // 버튼 위에서 왼쪽 버튼을 누르고 있음
	bool bClicked = false;   // 이번 프레임에 버튼 위에서 눌렀음
};

// 월드의 화면 위 UI. 매 프레임 비우고 새로 채움.
// 무엇을 그릴지 담아둠. ( FHUDRenderer 가 그림 )
class FHUD
{
public:
	// 월드 Tick 시작 시 호출. 지난 프레임 명령을 버린다.
	void BeginFrame() { Commands.Reset(); }

    // 사각형 추가
	void AddRect(const FVector2& Position, const FVector2& Size, const FLinearColor& Color)
	{
		FHUDDrawCommand Command;
		Command.Type = EHUDCommandType::Rect;
		Command.Position = Position;
		Command.Size = Size;
		Command.Color = Color;
		Commands.Add(std::move(Command));
	}

    // 텍스트 추가
	void AddText(const FString& Text, const FVector2& Position, float FontSize, const FLinearColor& Color)
	{
		FHUDDrawCommand Command;
		Command.Type = EHUDCommandType::Text;
		Command.Position = Position;
		Command.Text = Text;
		Command.FontSize = FontSize;
		Command.Color = Color;
		Commands.Add(std::move(Command));
	}

	// 월드 위치에 뜨는 텍스트. 화면 좌표는 렌더할 때 뷰포트의 FSceneView로 구하므로 실제 그려진 화면과 맞는다.
	// 카메라 뒤면 그리지 않는다. ScreenOffset은 투영 뒤 기준 단위로 더하는 값이다 (그림자 등).
	void AddWorldText(const FString& Text, const FVector& WorldPosition, float FontSize, const FLinearColor& Color,
		const FVector2& ScreenOffset = FVector2())
	{
		FHUDDrawCommand Command;
		Command.Type = EHUDCommandType::Text;
		Command.bWorldSpace = true;
		Command.WorldPosition = WorldPosition;
		Command.ScreenOffset = ScreenOffset;
		Command.Text = Text;
		Command.FontSize = FontSize;
		Command.Color = Color;
		Commands.Add(std::move(Command));
	}

	const TArray<FHUDDrawCommand>& GetCommands() const { return Commands; }

	// HUD가 그려지는 화면 크기. ( Viewport가 채움 )
	const FVector2& GetScreenSize() const { return ScreenSize; }
	void SetScreenSize(const FVector2& InSize) { ScreenSize = InSize; }

    // UI 좌표, 크기의 기준 해상도
	static constexpr float ReferenceWidth = 1920.0f;
	static constexpr float ReferenceHeight = 1080.0f;

	// HUD 글자 폰트 (게임 콘텐츠라 Assets에 있다). 없으면 엔진 기본 폰트로 그린다.
	// FHUDRenderer와 Lua의 MeasureHUDText가 같이 써야 글자 폭이 맞는다.
	static constexpr const char* FontPath = "Assets/Fonts/CookieRun.json";
	static constexpr const char* FontAtlasPath = "Assets/Fonts/CookieRun.png";
	static UFont* GetFont();

	// 기준 단위 -> 실제 픽셀 배율. 비율을 유지하도록 가로/세로 중 작은 쪽에 맞춤
	float GetScale() const
	{
		if (ScreenSize.X <= 0.0f || ScreenSize.Y <= 0.0f)
			return 1.0f;
		return std::min(ScreenSize.X / ReferenceWidth, ScreenSize.Y / ReferenceHeight);
	}

	// 기준 단위로 본 화면 크기
	FVector2 GetVirtualSize() const
	{
		const float Scale = GetScale();
		return FVector2(ScreenSize.X / Scale, ScreenSize.Y / Scale);
	}

    void AddImage(UTexture2D* Texture, const FVector2& Position, const FVector2& Size, const FLinearColor& Color,
		const FVector2& UVMin = FVector2(0.0f, 0.0f), const FVector2& UVMax = FVector2(1.0f, 1.0f));

	void SetInput(const FHUDInput& InInput)
	{
		Input = InInput;
		// 어디서든 마우스를 떼면 슬라이더 드래그가 끝난다
		if (!Input.bMouseDown)
			ActiveSliderId.clear();
	}
	// 포커스를 잃으면 입력과 함께 드래그도 끝낸다
	void ClearInput() { Input = FHUDInput{}; ActiveSliderId.clear(); }
	const FHUDInput& GetInput() const { return Input; }

	// 버튼 영역의 이번 프레임 입력 상태. 아무것도 그리지 않는다.
	FHUDButtonState GetButtonState(const FVector2& Position, const FVector2& Size) const;

	// 배경과 라벨을 그리고, 이번 프레임에 버튼 위에서 왼쪽 버튼을 눌렀으면 true
	bool Button(const FString& Label, const FVector2& Position, const FVector2& Size, float FontSize);

	// 이미지 버튼. 상태는 틴트로 표현한다 (Hover: 밝게, Pressed: 어둡게). Label이 비어 있으면 글자를 그리지 않는다.
	// HitMin/HitMax: 판정 영역을 이미지 안의 비율(0~1)로 준다. 기본은 이미지 전체.
	// 투명 여백이 겹치게 놓은 버튼끼리 한 번에 같이 눌리지 않도록 그림이 있는 부분만 판정할 때 쓴다.
	bool ImageButton(UTexture2D* Texture, const FVector2& Position, const FVector2& Size,
		const FString& Label = FString(), float FontSize = 24.0f,
		const FVector2& HitMin = FVector2(0.0f, 0.0f), const FVector2& HitMax = FVector2(1.0f, 1.0f));

	// 이미지 슬라이더. 트랙(Track) 위에서 손잡이(Handle)를 끌어 Value(0~1)를 바꾸고, 바뀐 값을 돌려준다.
	// Id: 프레임을 넘어 잡고 있는 슬라이더를 구분하는 이름. 잡은 뒤에는 마우스가 트랙 밖으로 나가도 따라간다.
	// MinU/MaxU: 트랙 폭 안에서 손잡이 가운데가 움직이는 구간(비율). 트랙 양끝 장식을 피할 때 쓴다.
	float ImageSlider(const FString& Id, UTexture2D* Track, UTexture2D* Handle,
		const FVector2& Position, const FVector2& Size, const FVector2& HandleSize,
		float Value, float MinU = 0.0f, float MaxU = 1.0f);

private:
	TArray<FHUDDrawCommand> Commands;
	FVector2 ScreenSize;
    FHUDInput Input;

	// 지금 잡고 있는 슬라이더 Id. 비어 있으면 잡은 슬라이더 없음.
	// Commands와 달리 BeginFrame에서 지우지 않는다 (프레임을 넘어 유지)
	FString ActiveSliderId;
};

// Script가 그릴 HUD 찾기
// GameWorld가 아니거나 월드 없으면 nullptr
FHUD* FindHUDForObject(UObject* Object);
