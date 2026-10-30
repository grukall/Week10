local Score = 0
local HP = 100
local Elapsed = 0

function BeginPlay()
    -- 코루틴으로 1초마다 점수 증가, 체력 감소
    start_coroutine(function()
        while HP > 0 do
            wait(1.0)
            Score = Score + 10
            HP = math.max(0, HP - 10)
        end
    end)
end

function Tick(dt)
    Elapsed = Elapsed + dt

    -- 왼쪽 위: 점수 패널
    DrawHUDRect(20, 20, 300, 90, FLinearColor(0.05, 0.05, 0.05, 0.7))
    DrawHUDText("Score: " .. Score, 32, 28, 28)

    -- 체력바 (배경 + 현재 값)
    DrawHUDRect(32, 70, 200, 20, FLinearColor(0.3, 0.3, 0.3, 1))
    DrawHUDRect(32, 70, HP * 2, 20, FLinearColor(0.9, 0.2, 0.2, 1))

    -- 오른쪽 위: 경과 시간 (가상 크기 기준 앵커)
    local W, H = GetHUDSize()
    DrawHUDText(string.format("%.1f s", Elapsed), W - 160, 28, 28, FLinearColor(1, 0.85, 0.3, 1))

    -- Button Test
    if HUDImageButton("Assets/Sprites/hitori.png", 760, 600, 400, 120) then
        print("Start")
    end

    if HUDImageButton("Assets/Sprites/hitori.png", 20, 380, 220, 64, "Reset", 28) then
    Score = 0
    end
    -- 체력이 0이면 화면 가운데 메시지
    if HP <= 0 then
        DrawHUDText("GAME OVER", W * 0.5 - 140, H * 0.5 - 30, 56, FLinearColor(1, 0.3, 0.3, 1))
    end
end