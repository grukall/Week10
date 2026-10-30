-- Player2 (우사기). ↑/↓ = +X / -X (카운터 쪽 / 입구 쪽), ←/→ = -Y / +Y (화면 왼쪽 / 오른쪽), Enter = 밀치기
-- 이동·회전은 Lib/PlayerController.lua 공통 코드를 쓴다. 벽·상대와의 충돌은 씬의 CollisionBox(UBoxComponent)가 맡는다.

local GameState = require("Assets.Scripts.Lib.GameState")

local Controller = dofile("Assets/Scripts/Lib/PlayerController.lua")({
	Forward = "Up", Back = "Down", Left = "Left", Right = "Right",
	PlayerNumber = 2, PushKey = "Enter",   -- 앞의 상대를 밀친다 (쿨타임 2초)
	KnockbackSound = "Assets/Audio/UsagiPush.wav",
	MeshYawOffset = 0,     -- usagi_chiikawa.obj는 정면이 +X를 보도록 변환했다
	SpawnOffsetY = 1.0,    -- 시작 위치: 바닥 중앙에서 화면 오른쪽으로
	OnPush = function()    -- 밀친 순간 잠깐 멈추고 화면을 흔든다 (Player1과 같은 값)
		HitStop(0.5)   -- TEMP: 확인용. 확인 후 0.08로 되돌린다
		CameraShake(0.1, 0.25)
	end,
})

-- 걷는 동안 발밑 연기를 켠다. 매 프레임 스폰하지 않고 걷기 시작·멈춤 순간에만 부른다
local FootOffset = FVector(0, 0, -0.5)   -- 루트(CollisionBox 중심)에서 발까지
local SmokeHandle = -1

function BeginPlay()
	GameState.RegisterPlayer(2, obj)
	Controller.BeginPlay(obj, GetPlayerController(1), EnableInput(1))
end

function Tick(dt)
	Controller.Tick(dt)

	local Moving = Controller.IsMoving()
	if Moving and SmokeHandle < 0 then
		SmokeHandle = SpawnEffectAttached("FootSmoke", obj, FootOffset, true)
	elseif not Moving and SmokeHandle >= 0 then
		StopEffect(SmokeHandle)   -- 남은 연기는 그 자리에서 마저 사라진다
		SmokeHandle = -1
	end
end

function EndPlay()
	Controller.EndPlay()
	SmokeHandle = -1   -- 이펙트는 월드가 정리할 때 함께 정리된다
end

function OnOverlap(OtherActor)
end
