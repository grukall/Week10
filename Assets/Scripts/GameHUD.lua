-- 인게임 진행과 HUD
--   Intro   : 시작 카메라 연출 (CameraIntro.lua). 이동·낙하 멈춤, 카운트다운 없음
--   Ready   : 카메라가 게임 시점에 도착한 뒤 가운데 3, 2, 1 카운트다운. 이동·낙하 멈춤
--   Playing : GameState.TimeLimit 동안 줍기. 상단 가운데 타이머(10초 아래면 Hurry), 왼쪽 P1 점수, 오른쪽 P2 점수
--   TimeOver: 가운데 TIME OVER! → 승자 엔딩 시퀀스(Assets/Sequences/Ending*.sequence)
--             두 캐릭터를 나란히 세우고 승자 쪽으로 카메라를 당긴 뒤 승자가 두 번 뛴다. 무승부면 둘 다 뛴다
--   Result  : 승자·무승부 영상 → 승자 표시 → 3등 안에 든 플레이어 이름 입력 → RANK 팝업과 다시하기·메인 메뉴 버튼
--   Esc     : Ready·Playing 중 일시정지 팝업 (음량 슬라이더·메인·재시작). 다시 누르면 이어 한다
-- 단계와 점수는 Lib/GameState.lua, 기록 저장은 Lib/Scoreboard.lua가 맡는다.
-- 상단 바·일시정지·결과 화면(배경·버튼·RANK 팝업)은 UI 이미지를 쓴다. 승자 글자와 이름 입력 칸은 글자로 그린다.

local GameState = require("Assets.Scripts.Lib.GameState")
local Scoreboard = require("Assets.Scripts.Lib.Scoreboard")
local ResultVideo = require("Assets.Scripts.Lib.ResultVideo")
local Sound = require("Assets.Scripts.Lib.Sound")

local GameScene = "Assets/Scenes/GameJamMap0.scene"
local TitleScene = "Assets/Scenes/GameJamTitle.scene"

-- 상단 UI 이미지. 크기는 HUD 기준 해상도(1920x1080) 단위, 세로는 원본 비율로 맞춘다
-- Anchor: 이미지 안에서 숫자를 올릴 곳의 가운데 (가로·세로 0~1 비율). 별 가운데, 캐릭터 몸통 가운데
local TimerImages = { Default = "Assets/UI/Timer/TIMER_Default.png", Hurry = "Assets/UI/Timer/TIMER_Hurry.png" }
local TimerSize = 260       -- 두 이미지 모두 정사각형이고 별 위치가 거의 같다
local TimerAnchorY = 0.647
local TimerTopY = -10       -- 이미지 위쪽은 비어 있어서 살짝 화면 밖으로 올린다

local ScoreImages = {
	{ Path = "Assets/UI/Score/CHIWI_SCORE.png", Aspect = 675 / 514, AnchorX = 0.76, AnchorY = 0.57 },
	{ Path = "Assets/UI/Score/USAGI_SCORE.png", Aspect = 684 / 450, AnchorX = 0.74, AnchorY = 0.70 },
}
local ScoreWidth = 320
local SideMargin = 24       -- 화면 양옆에서 점수 이미지까지

local TimeFontSize = 72
local ScoreFontSize = 64
local WarningTime = 10      -- 남은 시간이 이 아래면 타이머를 Hurry 이미지로
local GoShowTime = 0.8      -- 시작할 때 GO!를 보여 줄 시간 (초)
local TimeOverShowTime = 1.5   -- 시간이 다 되면 TIME OVER!를 보여 준 뒤 엔딩 시퀀스를 튼다 (초)
local EndingTimeout = 8        -- 엔딩 시퀀스가 끝났다는 신호가 없을 때 결과로 넘기는 시간 (초). 안전장치

-- 승자별 엔딩 시퀀스. [1] = P1(치이카와), [2] = P2(우사기), Draw = 무승부
local EndingSequences = {
	"Assets/Sequences/EndingP1.sequence",
	"Assets/Sequences/EndingP2.sequence",
	Draw = "Assets/Sequences/EndingDraw.sequence",
}

-- 일시정지 창. 팝업 이미지 위쪽에 PAUSE 글자가 있고, 그 아래 테두리 상자 안에 위부터 음량 슬라이더·메인·재시작을 놓는다.
-- 안쪽 요소의 X, Y, Width는 팝업 이미지 크기에 대한 비율(0~1)이다. X·Y는 왼쪽 위, 세로 크기는 원본 비율(Aspect)로 맞춘다
local PausePopup = { Path = "Assets/UI/Popup/PAUSE_POPUP.png", Aspect = 1854 / 2307, Height = 880 }
local PauseClose = { Right = 0.968, Y = 0.259, Width = 0.107 }   -- 테두리 상자 안쪽 오른쪽 위 X (누르면 이어 하기)
local MainButton = { Path = "Assets/UI/Buttons/MAIN_SIMPLE.png", Aspect = 911 / 266, X = 0.255, Y = 0.668, Width = 0.581 }
local RestartButton = { Path = "Assets/UI/Buttons/RESTART_SIMPLE.png", Aspect = 851 / 262, X = 0.249, Y = 0.816, Width = 0.551 }
-- 음량 슬라이더: BGM·SFX 글자와 물결 트랙은 팝업 이미지에 그려져 있어서 헤드폰 고양이(손잡이)만 그린다.
-- X, Y, Width, Height: 이미지 속 트랙(음표~끝 점)을 덮는 판정 영역. 손잡이는 이 영역의 세로 가운데에 놓인다.
-- MinU~MaxU: 손잡이 가운데가 움직이는 영역 폭 비율. 왼쪽 음표와 오른쪽 끝 점을 피한다
local SliderHandle = { Path = "Assets/UI/Backgrounds/SOUND_SLIDER_POINT.png", Width = 0.235 }   -- 정사각형 이미지
local VolumeSliders = {
	{ Id = "BGMVolume", Channel = "BGM", X = 0.112, Y = 0.390, Width = 0.772, Height = 0.070, MinU = 0.24, MaxU = 0.82 },
	{ Id = "SFXVolume", Channel = "SFX", X = 0.112, Y = 0.549, Width = 0.772, Height = 0.070, MinU = 0.24, MaxU = 0.82 },
}

local DarkText = FLinearColor(0.1, 0.1, 0.1, 1)   -- 밝은 이미지 위 숫자

-- 결과 화면 배경. [1] = P1 승, [2] = P2 승, Draw = 무승부
-- TitleX: 승자 글자·점수 줄의 가로 가운데 (화면 너비 비율). 배경의 말풍선을 피해서 둔다
local ResultScenes = {
	{ Path = "Assets/UI/Backgrounds/CHIWI_WIN_SCENE.png", Aspect = 4400 / 2476, TitleX = 0.5 },   -- 말풍선: 왼쪽 아래
	{ Path = "Assets/UI/Backgrounds/USAGI_WIN_SCENE.png", Aspect = 4400 / 2504, TitleX = 0.80 },  -- 말풍선: 왼쪽 위
	Draw = { Path = "Assets/UI/Backgrounds/DRAW_SCENE.png", Aspect = 4400 / 2588, TitleX = 0.87 }, -- 말풍선: 가운데 위
}
local ResultRestartButton = { Path = "Assets/UI/Buttons/RESTART_SIMPLE.png", Aspect = 851 / 262 }
local ResultMainButton = { Path = "Assets/UI/Buttons/MAIN_SIMPLE.png", Aspect = 911 / 266 }
local ResultButtonWidth = 360
local NamePanelColor = FLinearColor(0, 0, 0, 0.55)

local MaxNameLength = 8
local NameChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
-- 이름 입력이 시작된 뒤 이 시간(초) 동안은 키를 받지 않는다. P2 밀치기 키가 Enter라,
-- 판이 끝나는 순간 밀치려고 누른 Enter가 빈 이름으로 바로 확정되는 것을 막는다
local EntryInputDelay = 0.8

local HUDText = dofile("Assets/Scripts/Lib/HUDText.lua")({
	DrawHUDText = DrawHUDText, MeasureHUDText = MeasureHUDText, DrawHUDImage = DrawHUDImage,
	HUDImageButton = HUDImageButton })
local DrawText, DrawCentered = HUDText.Draw, HUDText.Centered

local White, Yellow, Gray = HUDText.White, HUDText.Yellow, HUDText.Gray
local DimColor = FLinearColor(0, 0, 0, 0.65)
local BoxColor = FLinearColor(1, 1, 1, 0.15)
local PlayerColors = { FLinearColor(1, 0.55, 0.6, 1), FLinearColor(0.55, 0.8, 1, 1) }

-- 결과 화면 상태
local Board = nil            -- Scoreboard.Load()로 읽은 기록
local EntryQueue = {}        -- 이름을 입력할 차례를 기다리는 플레이어 번호
local CurrentEntry = nil     -- { PlayerNumber, Name } 지금 이름을 입력하는 플레이어
local NewEntries = {}        -- 이번 판에 들어간 기록 (스코어보드에서 강조)

-- 다음 플레이어 차례로 넘긴다. 앞 사람이 들어가서 순위가 밀렸으면 그때 다시 따진다
local function StartNextEntry()
	CurrentEntry = nil
	while #EntryQueue > 0 do
		local PlayerNumber = table.remove(EntryQueue, 1)
		if Scoreboard.Qualifies(Board, GameState.GetScore(PlayerNumber)) then
			CurrentEntry = { PlayerNumber = PlayerNumber, Name = "", StartTime = GameState.PhaseTime }
			return
		end
	end
end

local function StartResult()
	GameState.SetPhase("Result")
	ResultVideo.Play(GameState.GetWinner())
	Board = Scoreboard.Load()
	NewEntries = {}

	-- 점수 높은 사람부터 입력한다. 같으면 P1 먼저
	EntryQueue = { 1, 2 }
	table.sort(EntryQueue, function(A, B)
		local SA, SB = GameState.GetScore(A), GameState.GetScore(B)
		if SA ~= SB then return SA > SB end
		return A < B
	end)
	StartNextEntry()
end

-- 엔딩 시퀀스 상태
local EndingSequence = nil   -- 재생 중인 시퀀스 액터. 끝나면 스스로 지워진다
local bEndingStarted = false

local function StartEnding()
	bEndingStarted = true
	EndingSequence = PlaySequence(EndingSequences[GameState.GetWinner() or "Draw"])
	if not EndingSequence then
		StartResult()   -- 시퀀스를 못 읽으면 바로 결과로
	end
end

-- 이름 입력 키. 결과 화면에서 입력 차례일 때만 받는다
-- 지금 입력 차례이고, 차례가 시작된 지 EntryInputDelay가 지났는지
local function CanTypeName()
	return not ResultVideo.IsPlaying() and CurrentEntry ~= nil
		and GameState.PhaseTime - CurrentEntry.StartTime >= EntryInputDelay
end

local function TypeChar(C)
	if CanTypeName() and #CurrentEntry.Name < MaxNameLength then
		CurrentEntry.Name = CurrentEntry.Name .. C
	end
end

local function Backspace()
	if CanTypeName() and #CurrentEntry.Name > 0 then
		CurrentEntry.Name = CurrentEntry.Name:sub(1, -2)
	end
end

local function ConfirmName()
	if not CanTypeName() then
		return
	end
	-- 비워 두고 Enter를 누르면 플레이어 이름으로 남긴다
	local Name = CurrentEntry.Name ~= "" and CurrentEntry.Name or ("P" .. CurrentEntry.PlayerNumber)
	local Rank = Scoreboard.Insert(Board, Name, GameState.GetScore(CurrentEntry.PlayerNumber), CurrentEntry.PlayerNumber)
	NewEntries[Board[Rank]] = true
	StartNextEntry()
end

local bBGMStarted = false

local function TogglePause()
	local bWasPaused = GameState.bPaused
	GameState.TogglePause()
	if GameState.bPaused ~= bWasPaused then
		if GameState.bPaused then
			Audio.Play("Assets/Audio/Pause_AraAra.wav")
		else
			HUDText.PlayClickSound()
		end
	end
end

function BeginPlay()
	ResultVideo.Reset()
	GameState.Reset()
	-- BGM은 시작 카메라 연출이 끝나고 Tick에서 튼다. 타이틀에서 이어지던 BGM은 연출 동안 끈다
	Audio.StopBGM()
	bBGMStarted = false

	-- 키보드는 하나라 0번 컨트롤러로 이름을 받는다. 이동 바인딩과 같이 올라가지만 단계가 달라 겹치지 않는다
	local Input = EnableInput(0)
	if Input then
		for i = 1, #NameChars do
			local C = NameChars:sub(i, i)
			Input:BindKey(C, "Pressed", function() TypeChar(C) end)
		end
		Input:BindKey("Backspace", "Pressed", Backspace)
		Input:BindKey("Enter", "Pressed", ConfirmName)
		Input:BindKey("Escape", "Pressed", TogglePause)
	end
end

-- 일시정지 창. 이미지가 정해지면 배경과 버튼만 DrawHUDImage·HUDImageButton으로 바꾼다
-- 글자를 (CenterX, CenterY)에 가운데 맞춰 그림자 없이 그린다. 글자 높이는 대략 Size로 본다
local function DrawTextAt(Text, CenterX, CenterY, Size, Color)
	DrawHUDText(Text, CenterX - MeasureHUDText(Text, Size) * 0.5, CenterY - Size * 0.55, Size, Color)
end

local function DrawPause(W, H)
	local CenterX = W * 0.5
	DrawHUDRect(0, 0, W, H, DimColor)

	local PopupH = PausePopup.Height
	local PopupW = PopupH * PausePopup.Aspect
	local PopupY = (H - PopupH) * 0.5
	DrawHUDImage(PausePopup.Path, CenterX - PopupW * 0.5, PopupY, PopupW, PopupH)

	local PopupX = CenterX - PopupW * 0.5

	-- 팝업 비율 좌표로 이미지 버튼을 그리고, 눌렀으면 true
	local function PopupButton(Button)
		local ButtonW = PopupW * Button.Width
		return HUDImageButton(Button.Path, PopupX + PopupW * Button.X, PopupY + PopupH * Button.Y, ButtonW, ButtonW / Button.Aspect)
	end

	local CloseSize = PopupW * PauseClose.Width
	if HUDText.CloseButton(PopupX + PopupW * PauseClose.Right, PopupY + PopupH * PauseClose.Y, CloseSize) then
		GameState.TogglePause()
		return
	end

	-- 음량: BGM·SFX 슬라이더. 돌려준 값이 바뀌었을 때만 반영한다
	local HandleSize = PopupW * SliderHandle.Width
	for _, Slider in ipairs(VolumeSliders) do
		local Current = Sound.GetVolume(Slider.Channel)
		local Volume = HUDImageSlider(Slider.Id, "", SliderHandle.Path,
			PopupX + PopupW * Slider.X, PopupY + PopupH * Slider.Y, PopupW * Slider.Width, PopupH * Slider.Height,
			HandleSize, HandleSize, Current, Slider.MinU, Slider.MaxU)
		if Volume ~= Current then
			Sound.SetVolume(Slider.Channel, Volume)
		end
	end

	-- 위: 메인, 아래: 재시작
	if PopupButton(MainButton) then
		HUDText.PlayClickSound()
		OpenScene(TitleScene)
	end
	if PopupButton(RestartButton) then
		HUDText.PlayClickSound()
		OpenScene(GameScene)
	end

	DrawTextAt("ESC: RESUME", CenterX, PopupY + PopupH + 40, 30, Gray)
end

local function DrawTopBar(W)
	-- 상단 가운데: 남은 시간(초). 10초 아래면 Hurry 이미지(빨간 별)로 바꾼다. 일시정지 중에도 그대로 둔다
	local Remaining = GameState.GetRemainingTime()
	local bHurry = GameState.Phase == "Playing" and Remaining <= WarningTime
	local TimerX = (W - TimerSize) * 0.5
	DrawHUDImage(bHurry and TimerImages.Hurry or TimerImages.Default, TimerX, TimerTopY, TimerSize, TimerSize)
	DrawTextAt(tostring(math.ceil(Remaining)), W * 0.5, TimerTopY + TimerSize * TimerAnchorY, TimeFontSize, bHurry and White or DarkText)

	-- 왼쪽: P1(치이카와), 오른쪽: P2(우사기). 점수는 캐릭터 몸통 위에
	for PlayerNumber, Image in ipairs(ScoreImages) do
		local ImageH = ScoreWidth / Image.Aspect
		local X = PlayerNumber == 1 and SideMargin or (W - SideMargin - ScoreWidth)
		DrawHUDImage(Image.Path, X, 0, ScoreWidth, ImageH)
		DrawTextAt(tostring(GameState.GetScore(PlayerNumber)), X + ScoreWidth * Image.AnchorX, ImageH * Image.AnchorY, ScoreFontSize, DarkText)
	end
end

local function DrawNameEntry(CenterX)
	-- 밝은 결과 배경 위에서도 글자가 읽히도록 입력 칸 둘레를 어둡게 깐다
	local PanelW = 960
	DrawHUDRect(CenterX - PanelW * 0.5, 420, PanelW, 280, NamePanelColor)

	local PlayerNumber = CurrentEntry.PlayerNumber
	DrawCentered("P" .. PlayerNumber .. "  NEW RECORD!  ENTER YOUR NAME", CenterX, 450, 44, PlayerColors[PlayerNumber])

	local BoxW, BoxH = 520, 90
	DrawHUDRect(CenterX - BoxW * 0.5, 520, BoxW, BoxH, BoxColor)
	-- 깜빡이는 커서
	local Cursor = (math.floor(GameState.PhaseTime * 2) % 2 == 0 and #CurrentEntry.Name < MaxNameLength) and "_" or ""
	DrawCentered(CurrentEntry.Name .. Cursor, CenterX, 537, 56)

	DrawCentered("A-Z 0-9   BACKSPACE: DELETE   ENTER: OK", CenterX, 640, 28, Gray)
end

local function DrawBoard(CenterX)
	-- 승자·점수 줄 아래부터 버튼 위까지 RANK 팝업
	HUDText.RankBoard(Board, CenterX, 250, 590, NewEntries)

	-- 버튼 두 개를 가운데 기준으로 나란히. 왼쪽: 재시작, 오른쪽: 메인
	local Gap, ButtonY = 40, 850
	local RestartH = ResultButtonWidth / ResultRestartButton.Aspect
	local MainH = ResultButtonWidth / ResultMainButton.Aspect
	if HUDImageButton(ResultRestartButton.Path, CenterX - Gap * 0.5 - ResultButtonWidth, ButtonY, ResultButtonWidth, RestartH) then
		HUDText.PlayClickSound()
		OpenScene(GameScene)
	end
	if HUDImageButton(ResultMainButton.Path, CenterX + Gap * 0.5, ButtonY, ResultButtonWidth, MainH) then
		HUDText.PlayClickSound()
		OpenScene(TitleScene)
	end
end

-- 배경 이미지를 비율을 지키며 화면을 꽉 채우게 그린다. 넘치는 쪽은 화면 밖으로 잘린다
local function DrawCover(Path, Aspect, W, H)
	local DrawW, DrawH = W, W / Aspect
	if DrawH < H then
		DrawW, DrawH = H * Aspect, H
	end
	DrawHUDImage(Path, (W - DrawW) * 0.5, (H - DrawH) * 0.5, DrawW, DrawH)
end

local function DrawResult(W, H)
	local CenterX = W * 0.5
	local Winner = GameState.GetWinner()
	local Scene = ResultScenes[Winner or "Draw"]
	DrawCover(Scene.Path, Scene.Aspect, W, H)

	-- 이름 입력 중에는 승자를 크게, 순위표를 보일 때는 팝업 자리를 내주려고 위로 작게 올린다.
	-- 가로 위치는 배경의 말풍선을 가리지 않게 배경마다 다르다
	local bBoard = CurrentEntry == nil
	local WinnerY, WinnerSize = bBoard and 50 or 170, bBoard and 100 or 120
	local ScoreY, ScoreSize = bBoard and 170 or 320, bBoard and 48 or 56
	local TitleX = W * Scene.TitleX

	if Winner then
		DrawCentered("P" .. Winner .. " WINS!", TitleX, WinnerY, WinnerSize, PlayerColors[Winner])
	else
		DrawCentered("DRAW", TitleX, WinnerY, WinnerSize, Yellow)
	end
	DrawCentered("P1  " .. GameState.GetScore(1) .. "   :   " .. GameState.GetScore(2) .. "  P2", TitleX, ScoreY, ScoreSize)

	if CurrentEntry then
		DrawNameEntry(CenterX)
	else
		DrawBoard(CenterX)
	end
end

local function DrawVideo(W, H)
	DrawHUDRect(0, 0, W, H, FLinearColor(0, 0, 0, 1))
	local Texture, U0, V0, U1, V1 = ResultVideo.GetVideoFrame()
	if Texture then
		local Clip = ResultVideo.Clip
		local Scale = math.min(960 / Clip.FrameWidth, W / Clip.FrameWidth, H / Clip.FrameHeight)
		local Width, Height = Clip.FrameWidth * Scale, Clip.FrameHeight * Scale
		DrawHUDImageRegion(Texture, (W - Width) * 0.5, (H - Height) * 0.5, Width, Height, U0, V0, U1, V1)
	end
end

function Tick(dt)
	local bWasVideoPlaying = ResultVideo.IsPlaying()
	ResultVideo.Update(dt)
	-- 일시정지 중에는 시간이 흐르지 않는다 (카운트다운·제한 시간 모두)
	if not GameState.bPaused then
		GameState.PhaseTime = GameState.PhaseTime + dt

		-- 연출이 끝났다는 신호가 오지 않으면(스크립트 오류 등) 일정 시간 뒤 그냥 시작한다
		if GameState.Phase == "Intro" and GameState.PhaseTime >= GameState.IntroTimeout then
			GameState.FinishIntro()
		elseif GameState.Phase == "Ready" and GameState.PhaseTime >= GameState.ReadyTime then
			GameState.SetPhase("Playing")
		elseif GameState.Phase == "Playing" and GameState.PhaseTime >= GameState.TimeLimit then
			GameState.SetPhase("TimeOver")
			bEndingStarted = false
		elseif GameState.Phase == "TimeOver" then
			if not bEndingStarted and GameState.PhaseTime >= TimeOverShowTime then
				StartEnding()
			elseif bEndingStarted and (not IsValid(EndingSequence) or GameState.PhaseTime >= TimeOverShowTime + EndingTimeout) then
				-- 시퀀스가 끝나 지워지면 결과 영상으로. 같은 프레임에 영상을 그려서 게임 화면이 비치지 않는다
				if IsValid(EndingSequence) then
					EndingSequence:Stop()
				end
				EndingSequence = nil
				StartResult()
			end
		end

		-- 카메라가 게임 시점에 도착하면(Intro가 끝나면) BGM을 튼다
		if not bBGMStarted and GameState.Phase ~= "Intro" then
			bBGMStarted = true
			Audio.PlayBGM("Assets/Audio/BGM.wav")
		end
	end

	-- 이름 입력 대기는 영상이 끝나 실제 결과 화면이 보이는 순간부터 센다.
	if bWasVideoPlaying and not ResultVideo.IsPlaying() and CurrentEntry then
		CurrentEntry.StartTime = GameState.PhaseTime
	end

	local W, H = GetHUDSize()
	if ResultVideo.IsPlaying() then
		DrawVideo(W, H)
		return
	end
	-- 엔딩 시퀀스 동안은 상단 바를 숨겨 화면을 비운다
	if GameState.Phase == "TimeOver" and bEndingStarted then
		return
	end
	DrawTopBar(W)

	if GameState.bPaused then
		DrawPause(W, H)
	elseif GameState.Phase == "Ready" then
		local Count = math.ceil(GameState.ReadyTime - GameState.PhaseTime)
		DrawCentered(tostring(Count), W * 0.5, H * 0.5 - 120, 200, Yellow)
	elseif GameState.Phase == "Playing" and GameState.PhaseTime < GoShowTime then
		DrawCentered("GO!", W * 0.5, H * 0.5 - 120, 200, Yellow)
	elseif GameState.Phase == "TimeOver" then
		DrawCentered("TIME OVER!", W * 0.5, H * 0.5 - 120, 200, Yellow)
	elseif GameState.Phase == "Result" then
		DrawResult(W, H)
	end
end

function EndPlay()
	if IsValid(EndingSequence) then
		EndingSequence:Stop()
	end
	EndingSequence = nil
	ResultVideo.Reset()
end
