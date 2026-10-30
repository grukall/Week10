-- 타이틀 화면: 배경 + 왼쪽 위 타이틀 로고 + 왼쪽 아래 시작 / 나가기 버튼, 아래 줄 순위(RANK) / 조작법(HOW TO) / 만든 사람(CREDIT) 버튼
-- 버튼은 이미지 버튼이라 마우스를 올리면 밝게, 누르는 중이면 어둡게 그려진다.
-- 순위·조작법 창은 테두리 상자 오른쪽 위, 크레딧 화면은 화면 오른쪽 위 X 버튼으로 닫는다.

local Scoreboard = require("Assets.Scripts.Lib.Scoreboard")
local HUDText = dofile("Assets/Scripts/Lib/HUDText.lua")({
    DrawHUDText = DrawHUDText, MeasureHUDText = MeasureHUDText, DrawHUDImage = DrawHUDImage,
    HUDImageButton = HUDImageButton })

local GameScene = "Assets/Scenes/GameJamMap0.scene"

local Background  = "Assets/UI/Backgrounds/Main.png"

-- 배치는 HUD 기준 해상도(1920x1080)에서 이미지 왼쪽 위(X, Y)와 가로 폭(Width)으로 적는다. 세로는 Aspect(가로/세로)로 맞춘다.
-- 화면 비율이 다르면 위치만 화면 크기에 비례해 옮기고 크기는 그대로 둔다.
local ReferenceW, ReferenceH = 1920, 1080

local Title = { Path = "Assets/UI/Logos/Title.png", Aspect = 432 / 238, X = 50, Y = 100, Width = 970 }

-- Hit: 그림이 있는 부분만 누를 수 있게 이미지 안의 판정 영역(왼, 위, 오른, 아래 비율).
-- 버튼 이미지마다 투명 여백이 있어서 START와 EXIT처럼 겹쳐 놓으면 여백이 서로의 그림을 덮는다.
local Buttons = {
    Start  = { Path = "Assets/UI/Buttons/START.png",  Aspect = 1645 / 962, X = 70,   Y = 647, Width = 445, Hit = { 0.085, 0.179, 0.999, 0.811 } },
    Exit   = { Path = "Assets/UI/Buttons/EXIT.png",   Aspect = 1655 / 977, X = 68,   Y = 797, Width = 445, Hit = { 0.076, 0.188, 0.999, 0.827 } },
    Rank   = { Path = "Assets/UI/Buttons/RANK.png",   Aspect = 1463 / 691, X = 576,  Y = 824, Width = 411, Hit = { 0.014, 0.113, 0.967, 0.999 } },
    HowTo  = { Path = "Assets/UI/Buttons/HowTo.png",  Aspect = 1537 / 874, X = 1003, Y = 814, Width = 425, Hit = { 0.073, 0.158, 0.999, 0.835 } },
    Credit = { Path = "Assets/UI/Buttons/CREDIT.png", Aspect = 1518 / 769, X = 1472, Y = 840, Width = 382, Hit = { 0.025, 0.091, 0.999, 0.900 } },
}

local CloseSize = 90        -- 창 오른쪽 위 X 버튼 크기

-- 조작법 팝업. 키 안내가 이미지에 그려져 있어서 키를 바꾸면 이미지도 같이 고친다.
-- CloseTop: 테두리 상자 윗변(이미지 위에서 23%)보다 조금 아래, PAUSE 팝업과 같은 방식
local HowToPopup = { Path = "Assets/UI/Popup/HOWTO_POPUP.png", Aspect = 2462 / 2270, Height = 820, CloseTop = 0.26 }

-- 크레딧 화면. 빈 띠 없이 꽉 찬 그림이라 이미지 전체(ContentBottom = 1)가 화면을 덮도록 키운다
local CreditScene = { Path = "Assets/UI/Backgrounds/CREDIT_SCENE.png", Aspect = 3428 / 1935, ContentBottom = 1.0 }
local CreditCloseMargin = 40   -- 화면 오른쪽 위 모서리에서 X 버튼까지

local DimColor = FLinearColor(0, 0, 0, 0.75)

-- 열린 창: nil | "Scoreboard" | "HowTo" | "Credit"
local OpenPanel = nil
local OpenBoard = nil   -- 스코어보드 창을 열 때마다 파일에서 다시 읽는다

-- 기준 해상도 좌표를 지금 화면으로 옮겨 그리고, 판정 영역 안을 눌렀으면 true
local function LayoutButton(Button, W, H)
    local X = Button.X * W / ReferenceW
    local Y = Button.Y * H / ReferenceH
    local Hit = Button.Hit
    return HUDImageButtonArea(Button.Path, X, Y, Button.Width, Button.Width / Button.Aspect,
        Hit[1], Hit[2], Hit[3], Hit[4])
end

-- 버튼 그림만 그린다 (누를 수 없음). 시작·나가기 소리를 기다리는 동안 나머지 버튼이 사라지지 않게 쓴다
local function DrawButtonImage(Button, W, H)
    DrawHUDImage(Button.Path, Button.X * W / ReferenceW, Button.Y * H / ReferenceH, Button.Width, Button.Width / Button.Aspect)
end

-- RANK 팝업. 테두리 상자 오른쪽 위에 X
local function DrawScoreboardPanel(W, H)
    local PopupH = 820
    local CloseRight, CloseTop = HUDText.RankBoard(OpenBoard, W * 0.5, (H - PopupH) * 0.5, PopupH)
    if HUDText.CloseButton(CloseRight, CloseTop, CloseSize) then
        OpenPanel = nil
    end
end

-- HOW TO 팝업. 화면 가운데에 그리고 테두리 상자 오른쪽 위에 X
local function DrawHowToPanel(W, H)
    local CenterX = W * 0.5
    local PopupH = HowToPopup.Height
    local PopupW = PopupH * HowToPopup.Aspect
    local PopupY = (H - PopupH) * 0.5
    DrawHUDImage(HowToPopup.Path, CenterX - PopupW * 0.5, PopupY, PopupW, PopupH)

    if HUDText.CloseButton(CenterX + PopupW * 0.46, PopupY + PopupH * HowToPopup.CloseTop, CloseSize) then
        OpenPanel = nil
    end
end

-- 크레딧 화면. 그림 부분이 화면 전체를 덮게 가로 가운데·위에 맞추고, 화면 오른쪽 위에 X
local function DrawCreditPanel(W, H)
    local ContentAspect = CreditScene.Aspect / CreditScene.ContentBottom
    local DrawW = math.max(W, H * ContentAspect)
    DrawHUDImage(CreditScene.Path, (W - DrawW) * 0.5, 0, DrawW, DrawW / CreditScene.Aspect)

    if HUDText.CloseButton(W - CreditCloseMargin, CreditCloseMargin, CloseSize) then
        OpenPanel = nil
    end
end

local ActionPending = false
local StartSound = nil
local QuitSound = nil

function BeginPlay()
    ActionPending = false
    StartSound = nil
    QuitSound = nil
    OpenPanel = nil
    OpenBoard = nil
    Audio.PlayBGM("Assets/Audio/BGM.wav")
end

function Tick(dt)
    local W, H = GetHUDSize()

    -- 크레딧은 화면 전체를 덮는 장면이라 타이틀 위에 따로 그린다
    if OpenPanel == "Credit" then
        DrawCreditPanel(W, H)
        return
    end

    -- 배경은 화면 전체를 덮는다
    DrawHUDImage(Background, 0, 0, W, H)

    -- 타이틀 로고: 왼쪽 위
    DrawHUDImage(Title.Path, Title.X * W / ReferenceW, Title.Y * H / ReferenceH, Title.Width, Title.Width / Title.Aspect)

    -- 창이 열려 있으면 그 위에 창만 그리고, 아래 버튼은 누를 수 없게 한다
    if OpenPanel then
        DrawHUDRect(0, 0, W, H, DimColor)
        if OpenPanel == "Scoreboard" then
            DrawScoreboardPanel(W, H)
        else
            DrawHowToPanel(W, H)
        end
        return
    end

    -- 시작·나가기 소리가 끝나길 기다리는 중. 버튼은 그리기만 하고 다시 받지 않는다
    -- HUD는 매 프레임 그린 것만 보이므로 아래 줄 버튼도 그림으로 계속 그린다
    if ActionPending then
        LayoutButton(Buttons.Start, W, H)
        LayoutButton(Buttons.Exit, W, H)
        DrawButtonImage(Buttons.Rank, W, H)
        DrawButtonImage(Buttons.HowTo, W, H)
        DrawButtonImage(Buttons.Credit, W, H)
        if StartSound ~= nil and not Audio.IsPlaying(StartSound) then
            StartSound = nil
            OpenScene(GameScene)
        elseif QuitSound ~= nil and not Audio.IsPlaying(QuitSound) then
            QuitSound = nil
            QuitGame()
        end
        return
    end

    -- 왼쪽 아래: 시작 위에 나가기를 겹쳐 그린다. 판정이 살짝 겹치는 곳은 위에 그려진 나가기가 이긴다
    -- 소리를 재생하고, 소리가 끝나면 장면을 바꾸거나 종료한다. 재생에 실패하면(0) 바로 한다
    local bStart = LayoutButton(Buttons.Start, W, H)
    local bExit = LayoutButton(Buttons.Exit, W, H)
    if bExit or bStart then
        -- 이번 프레임에 아래 줄 버튼을 건너뛰면 한 프레임 깜빡이므로 그림은 먼저 그려 둔다
        DrawButtonImage(Buttons.Rank, W, H)
        DrawButtonImage(Buttons.HowTo, W, H)
        DrawButtonImage(Buttons.Credit, W, H)
    end
    if bExit then
        ActionPending = true
        Audio.StopBGM()
        QuitSound = Audio.Play("Assets/Audio/Quit.wav")
        if QuitSound == 0 then
            QuitSound = nil
            QuitGame()
        end
        return
    elseif bStart then
        ActionPending = true
        StartSound = Audio.Play("Assets/Audio/Start_Oi.wav")
        if StartSound == 0 then
            StartSound = nil
            OpenScene(GameScene)
        end
        return
    end

    -- 아래 줄: 순위 / 조작법 / 크레딧
    if LayoutButton(Buttons.Rank, W, H) then
        HUDText.PlayClickSound()
        OpenBoard = Scoreboard.Load()
        OpenPanel = "Scoreboard"
    end
    if LayoutButton(Buttons.HowTo, W, H) then
        HUDText.PlayClickSound()
        OpenPanel = "HowTo"
    end
    if LayoutButton(Buttons.Credit, W, H) then
        HUDText.PlayClickSound()
        OpenPanel = "Credit"
    end
end
