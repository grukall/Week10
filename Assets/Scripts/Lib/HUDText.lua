-- HUD 글자 그리기 도우미와 순위표(RANK 팝업). 타이틀(GameTitle)과 인게임(GameHUD)이 같이 쓴다.
-- DrawHUDText 등은 스크립트마다 따로 등록되는 함수라 dofile로 읽은 이 파일에서는 보이지 않는다.
-- 그래서 만들 때 넘겨받는다.
--   local HUDText = dofile("Assets/Scripts/Lib/HUDText.lua")({
--       DrawHUDText = DrawHUDText, MeasureHUDText = MeasureHUDText, DrawHUDImage = DrawHUDImage,
--       HUDImageButton = HUDImageButton })

-- 순위표 팝업. 테두리 상자 안(CardTop~CardBottom 비율)에 카드 3장을 삼각형으로 놓는다: 위 가운데 1등, 아래 왼쪽 2등, 아래 오른쪽 3등.
-- 왼쪽 위 RANK 글자는 1등 카드(가운데)와 가로로 겹치지 않아서 상자 위쪽부터 쓴다
local RankPopup = { Path = "Assets/UI/Popup/RANK_POPUP.png", Aspect = 2901 / 2331, CardTop = 0.22, CardBottom = 0.93 }
local CardGap = 0.025   -- 카드 사이 간격 (팝업 너비 대비)

-- 순위 카드. 그림에 순위(N급)와 캐릭터 얼굴이 있고, 위쪽 띠에 이름, 아래 오른쪽에 점수를 올린다 (카드 안 0~1 비율)
local CardAspect = 1112 / 734
local CardImages = {
	{ "Assets/UI/RANK/CHIWI_FIRST.png", "Assets/UI/RANK/CHIWI_SECOND.png", "Assets/UI/RANK/CHIWI_THIRD.png" },
	{ "Assets/UI/RANK/USAGI_FIRST.png", "Assets/UI/RANK/USAGI_SECOND.png", "Assets/UI/RANK/USAGI_THIRD.png" },
}
-- 팝업 닫기(X) 버튼. 정사각형
local CloseImage = "Assets/UI/Buttons/X.png"
local ClickSound = "Assets/Audio/UIClick_Yes.wav"
local CloseSound = "Assets/Audio/UIClose_No.wav"

local CardName = { X = 0.33, Y = 0.32, Size = 0.16 }    -- Size는 카드 높이 대비 글자 크기
local CardScore = { X = 0.76, Y = 0.72, Size = 0.20 }

return function(Api)
	local HUDText = {
		White = FLinearColor(1, 1, 1, 1),
		Yellow = FLinearColor(1, 0.85, 0.2, 1),
		Gray = FLinearColor(0.75, 0.75, 0.75, 1),
		Shadow = FLinearColor(0, 0, 0, 0.6),
	}
	local ShadowOffset = 3

	-- 배경이 밝아도 읽히도록 살짝 어긋난 그림자를 먼저 그린다
	function HUDText.Draw(Text, X, Y, Size, Color)
		Api.DrawHUDText(Text, X + ShadowOffset, Y + ShadowOffset, Size, HUDText.Shadow)
		Api.DrawHUDText(Text, X, Y, Size, Color or HUDText.White)
	end

	function HUDText.Centered(Text, CenterX, Y, Size, Color)
		HUDText.Draw(Text, CenterX - Api.MeasureHUDText(Text, Size) * 0.5, Y, Size, Color)
	end

	function HUDText.RightAligned(Text, RightX, Y, Size, Color)
		HUDText.Draw(Text, RightX - Api.MeasureHUDText(Text, Size), Y, Size, Color)
	end

	local DarkText = FLinearColor(0.1, 0.1, 0.1, 1)

	-- 그림자 없이 (CenterX, CenterY)에 가운데 맞춰 그린다. 밝은 이미지 위 글자용. 글자 높이는 대략 Size로 본다
	function HUDText.DrawAt(Text, CenterX, CenterY, Size, Color)
		Api.DrawHUDText(Text, CenterX - Api.MeasureHUDText(Text, Size) * 0.5, CenterY - Size * 0.55, Size, Color or DarkText)
	end

	function HUDText.PlayClickSound()
		Audio.Play(ClickSound)
	end

	-- 오른쪽 위 모서리가 (RightX, TopY)인 X 버튼. 눌렀으면 소리를 재생하고 true
	function HUDText.CloseButton(RightX, TopY, Size)
		local Clicked = Api.HUDImageButton(CloseImage, RightX - Size, TopY, Size, Size)
		if Clicked then Audio.Play(CloseSound) end
		return Clicked
	end

	-- 화면 가로 가운데, TopY부터 Height 높이로 RANK 팝업을 그리고 1~3등 카드를 삼각형으로 놓는다.
	-- 카드 그림은 기록한 플레이어(치이카와/우사기)와 순위로 고른다. 빈 순위는 ---.
	-- Highlight[기록] = true면 카드 위에 NEW! (이번 판에 들어간 기록)
	-- 팝업 테두리 상자의 오른쪽 위 모서리(X 버튼 자리)를 돌려준다
	function HUDText.RankBoard(Board, CenterX, TopY, Height, Highlight)
		local PopupW = Height * RankPopup.Aspect
		local PopupLeft = CenterX - PopupW * 0.5
		Api.DrawHUDImage(RankPopup.Path, PopupLeft, TopY, PopupW, Height)

		-- 카드 크기: 두 줄이 세로에 들어가고, 아랫줄 두 장이 가로에 들어가는 것 중 작은 쪽
		local Gap = PopupW * CardGap
		local AreaTop, AreaH = TopY + Height * RankPopup.CardTop, Height * (RankPopup.CardBottom - RankPopup.CardTop)
		local AreaW = PopupW * 0.88
		local CardH = math.min((AreaH - Gap) * 0.5, ((AreaW - Gap) * 0.5) / CardAspect)
		local CardW = CardH * CardAspect

		-- 두 줄 묶음을 영역 세로 가운데에 놓는다
		local TopRowY = AreaTop + (AreaH - (CardH * 2 + Gap)) * 0.5
		local BottomRowY = TopRowY + CardH + Gap
		local Slots = {
			{ X = CenterX - CardW * 0.5,       Y = TopRowY },      -- 1등: 위 가운데
			{ X = CenterX - Gap * 0.5 - CardW, Y = BottomRowY },   -- 2등: 아래 왼쪽
			{ X = CenterX + Gap * 0.5,         Y = BottomRowY },   -- 3등: 아래 오른쪽
		}

		for Rank = 1, 3 do
			local X, CardY = Slots[Rank].X, Slots[Rank].Y
			local Entry = Board[Rank]
			if Entry then
				Api.DrawHUDImage(CardImages[Entry.Player or 1][Rank], X, CardY, CardW, CardH)
				HUDText.DrawAt(Entry.Name, X + CardW * CardName.X, CardY + CardH * CardName.Y, CardH * CardName.Size)
				HUDText.DrawAt(tostring(Entry.Score), X + CardW * CardScore.X, CardY + CardH * CardScore.Y, CardH * CardScore.Size)
				if Highlight and Highlight[Entry] then
					HUDText.DrawAt("NEW!", X + CardW * 0.5, CardY - CardH * 0.1, CardH * 0.16, FLinearColor(0.9, 0.3, 0.3, 1))
				end
			else
				HUDText.DrawAt("---", X + CardW * 0.5, CardY + CardH * 0.5, CardH * 0.2, HUDText.Gray)
			end
		end

		-- 테두리 상자는 팝업 이미지의 오른쪽 끝·위에서 15% 지점부터 시작한다
		return PopupLeft + PopupW * 0.97, TopY + Height * 0.17
	end

	return HUDText
end
