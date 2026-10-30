-- 한 판의 진행 단계·점수. 여러 스크립트가 같은 값을 봐야 해서 require로 불러 쓴다.
--   local GameState = require("Assets.Scripts.Lib.GameState")
-- dofile은 부를 때마다 새 테이블을 만들지만, require는 처음 한 번만 실행하고 같은 테이블을 돌려준다.
-- 같은 이유로 PIE를 다시 시작해도 값이 남아 있으므로 GameHUD의 BeginPlay가 Reset을 부른다.
-- (require는 파일을 다시 읽지 않으므로 이 파일을 고치면 에디터를 다시 켜야 반영된다)
--
-- 진행: Intro(시작 카메라 연출) → Ready(시작 대기) → Playing(제한 시간 동안 줍기)
--       → TimeOver(TIME OVER! 글자 → 승자 엔딩 시퀀스) → Result(결과 영상·이름 입력·스코어보드)
-- Intro는 CameraIntro가 FinishIntro로 끝내고, 나머지 단계를 넘기는 건 GameHUD가 한다.
-- 다른 스크립트는 IsPlaying으로 지금 움직여도 되는지만 본다.

local GameState = {
	IntroTimeout = 10,   -- 카메라 연출이 끝났다고 알려 오지 않을 때 Ready로 넘기는 시간 (초). 안전장치
	ReadyTime = 3,       -- 시작 전 대기 (초). 이동·낙하·점수 모두 멈춘다
	TimeLimit = 15,      -- 제한 시간 (초)

	Phase = "Intro",     -- "Intro" | "Ready" | "Playing" | "Result"
	PhaseTime = 0,       -- 지금 단계에 들어온 뒤 지난 시간 (초)
	bPaused = false,     -- Esc 일시정지. 켜져 있으면 시간·이동·낙하·점수가 모두 멈춘다
	Scores = { 0, 0 },   -- [1] = P1, [2] = P2
	Players = {},        -- [1] = P1 액터, [2] = P2 액터. 낙하물을 누가 주웠는지 찾을 때 쓴다
	-- 밀쳐진 플레이어에게 보내는 우편함. [번호] = { X, Y } (밀려날 방향). 다른 스크립트의 함수를 직접 부를 수 없어서
	-- 미는 쪽이 여기에 넣고, 밀리는 쪽이 자기 Tick에서 꺼내 간다
	Knockbacks = {},
}

-- 단계·점수만 비운다. Players는 BeginPlay 순서상 Reset보다 먼저 등록될 수 있어 지우지 않는다.
-- 지난 PIE의 액터는 파괴된 핸들이라 비교해도 같지 않고, 새 판에서 RegisterPlayer가 덮어쓴다.
function GameState.Reset()
	GameState.Phase = "Intro"
	GameState.PhaseTime = 0
	GameState.bPaused = false
	GameState.Scores = { 0, 0 }
	GameState.Knockbacks = {}
end

-- PlayerNumber를 (DirX, DirY) 방향으로 밀친다. 방향은 정규화해서 넘긴다
function GameState.PushPlayer(PlayerNumber, DirX, DirY)
	GameState.Knockbacks[PlayerNumber] = { X = DirX, Y = DirY }
end

-- 나에게 온 밀침을 꺼낸다. 없으면 nil
function GameState.TakeKnockback(PlayerNumber)
	local Knockback = GameState.Knockbacks[PlayerNumber]
	GameState.Knockbacks[PlayerNumber] = nil
	return Knockback
end

function GameState.SetPhase(Phase)
	GameState.Phase = Phase
	GameState.PhaseTime = 0
end

-- 시작 카메라 연출이 끝나면 부른다. 이때부터 Ready 카운트다운이 시작된다
function GameState.FinishIntro()
	if GameState.Phase == "Intro" then
		GameState.SetPhase("Ready")
	end
end

-- 제한 시간 안이고 일시정지가 아닐 때만 true
function GameState.IsPlaying()
	return GameState.Phase == "Playing" and not GameState.bPaused
end

-- 월드가 멈춰 있어야 하는지: 일시정지 중이거나 판이 끝난 뒤(엔딩·결과 화면). 떨어지던 것까지 그 자리에 멈춘다
function GameState.IsFrozen()
	return GameState.bPaused or GameState.Phase == "TimeOver" or GameState.Phase == "Result"
end

-- 시작 대기·제한 시간 중에만 멈출 수 있다. 엔딩·결과 화면에서는 무시한다 (시퀀스는 일시정지로 멈추지 않는다)
function GameState.TogglePause()
	if GameState.Phase == "TimeOver" or GameState.Phase == "Result" then
		return
	end
	GameState.bPaused = not GameState.bPaused
end

-- 화면에 보일 남은 시간. 연출·대기 중에는 꽉 찬 제한 시간, 끝나면 0
function GameState.GetRemainingTime()
	if GameState.Phase == "Intro" or GameState.Phase == "Ready" then
		return GameState.TimeLimit
	elseif GameState.Phase == "Playing" then
		return math.max(0, GameState.TimeLimit - GameState.PhaseTime)
	end
	return 0
end

-- 플레이어 스크립트의 BeginPlay에서 부른다
function GameState.RegisterPlayer(PlayerNumber, Actor)
	GameState.Players[PlayerNumber] = Actor
end

-- 등록된 플레이어면 번호(1, 2), 아니면 nil
function GameState.FindPlayerNumber(Actor)
	for PlayerNumber, Player in pairs(GameState.Players) do
		if Player == Actor then
			return PlayerNumber
		end
	end
	return nil
end

-- PlayerNumber: 1 = P1, 2 = P2. 제한 시간 안에서만 점수가 바뀐다
function GameState.AddScore(PlayerNumber, Amount)
	if not GameState.IsPlaying() then
		return
	end
	GameState.Scores[PlayerNumber] = (GameState.Scores[PlayerNumber] or 0) + Amount
end

function GameState.GetScore(PlayerNumber)
	return GameState.Scores[PlayerNumber] or 0
end

-- 1 또는 2, 같으면 nil (무승부)
function GameState.GetWinner()
	local P1, P2 = GameState.GetScore(1), GameState.GetScore(2)
	if P1 > P2 then return 1 end
	if P2 > P1 then return 2 end
	return nil
end

return GameState
