-- 플레이어 공통 이동: 키 입력 이동 + 이동 방향 회전(lerp)
-- 충돌은 엔진 Box 컴포넌트가 맡는다. 플레이어의 CollisionBox와 씬의 벽(Wall*)이 모두 bBlockComponent라서
-- AddActorWorldOffset(Delta, true)로 옮기면 엔진이 Sweep해 벽·다른 플레이어 앞에서 멈춘다.
-- 플레이어 스크립트에서 dofile로 불러 Config를 넘겨 만든다.
--   local Controller = dofile("Assets/Scripts/Lib/PlayerController.lua")({ Forward = "W", Back = "S", Left = "A", Right = "D" })
--
-- 입력은 키를 직접 읽지 않고 엔진의 PlayerController에서 받는다.
--   포커스된 뷰포트 → PlayerController(키 매핑) → 이 액터의 InputComponent → BindAxis 함수
-- dofile로 읽은 파일은 플레이어 스크립트의 environment를 모르므로 EnableInput·GetPlayerController 결과를 BeginPlay로 넘겨받는다.

-- Lua 5.1·LuaJIT는 math.atan2, 5.3 이상은 math.atan(y, x)를 쓴다
local Atan2 = math.atan2 or math.atan

-- 바닥 범위 (월드 좌표). 시작 위치와 이동 범위에 쓴다.
-- 벽 Box가 1차로 막지만, 밀려날 때처럼 빠르면 넘어갈 수 있어서 원형 플레이 영역(PlayerRadius) 안으로 한 번 더 가둔다
local Floor = dofile("Assets/Scripts/Lib/Floor.lua")

-- 시작 대기·결과 화면에서는 움직이지 않는다
local GameState = require("Assets.Scripts.Lib.GameState")

-- 각도 차이를 -180 ~ 180으로 접는다. 350도 → 10도로 갈 때 340도를 거꾸로 돌지 않고 20도만 돈다
local function WrapAngle(Degrees)
	return (Degrees + 180) % 360 - 180
end

return function(Config)
	local Speed = Config.Speed or 3.0               -- 초당 이동 거리
	local TurnSpeed = Config.TurnSpeed or 12.0      -- 회전 lerp 속도. 클수록 빨리 돈다
	local MeshYawOffset = Config.MeshYawOffset or 0 -- 메시 정면이 +X가 아닐 때 보정 (도)
	local SpawnOffsetY = Config.SpawnOffsetY        -- 있으면 시작할 때 바닥 중앙에서 Y로 이만큼 떨어진 곳에 선다

	-- 밀쳐내기: 앞쪽 부채꼴 안에 상대가 있으면 상대를 뒤로 밀어낸다
	local PlayerNumber = Config.PlayerNumber        -- 1 = P1, 2 = P2
	local PushCooldown = Config.PushCooldown or 2.0 -- 다시 밀 수 있을 때까지 (초). 실제로 밀었을 때만 돈다
	local PushRange = Config.PushRange or 1.5       -- 이 거리 안의 상대만 민다
	local PushHalfAngle = Config.PushHalfAngle or 60 -- 바라보는 방향에서 좌우 이 각도(도) 안의 상대만 민다
	-- 밀려나는 거리·시간은 액터의 Knockback 컴포넌트(UKnockbackComponent)에 있다. 밀려나는 동안은 조작 불가
	-- 맞밀기 유예: 밀려나기 시작한 지 이 시간(초) 안에 누르면 밀려나는 중이어도 상대를 민다.
	-- 사람이 "동시에" 눌러도 수 프레임 차이가 나서, 이게 없으면 먼저 누른 쪽만 민다
	local TradeWindow = Config.TradeWindow or 0.15
	-- 실제로 밀었을 때 부른다 (히트스톱·카메라 흔들림 같은 타격 연출).
	-- HitStop 등은 액터 스크립트 환경에만 등록돼 이 파일(dofile)에서는 보이지 않아 콜백으로 받는다
	local OnPush = Config.OnPush

	local Actor = nil
	local Root = nil
	local TargetYaw = 0
	-- PlayerController가 매 프레임 넣어 주는 Axis 값 (-1 ~ 1)
	local AxisForward = 0
	local AxisRight = 0
	local bMoving = false   -- 이번 프레임에 이동 입력이 있었는지 (발밑 연기 등에 쓴다)

	local PushCooldownLeft = 0
	local Knockback = nil   -- UKnockbackComponent: 밀려나기 (C++)
	local HitSquash = nil   -- UHitSquashComponent: 맞은 순간 찌그러지기 (C++)
	local bTraded = false   -- 이번 밀림에서 이미 맞밀었는지

	local Controller = {}

	-- 앞에 상대가 있으면 밀친다. Push 바인딩이 부른다 (액터 Tick보다 먼저)
	local function TryPush()
		-- 밀려나는 중에는 못 밀지만, 맞은 직후 유예 시간 안이면 한 번 맞밀 수 있다
		local bKnockedBack = Knockback:IsKnockedBack()
		local bTrade = bKnockedBack and not bTraded and Knockback:GetElapsedTime() <= TradeWindow
		if not GameState.IsPlaying() or PushCooldownLeft > 0 or (bKnockedBack and not bTrade) then
			return
		end
		local Other = GameState.Players[3 - PlayerNumber]
		if not IsValid(Other) then
			return
		end

		local MyLocation = Actor:GetActorLocation()
		local OtherLocation = Other:GetActorLocation()
		local DX = OtherLocation.X - MyLocation.X
		local DY = OtherLocation.Y - MyLocation.Y
		local Distance = math.sqrt(DX * DX + DY * DY)
		-- 맞밀기면 거리는 보지 않는다. 그 사이 이미 밀려나 멀어졌기 때문이다 (마주 보는지는 아래에서 그대로 본다)
		if Distance < 0.001 or (Distance > PushRange and not bTrade) then
			return
		end

		-- 바라보는 방향은 메시 보정을 뺀 TargetYaw. 회전 lerp가 끝나기 전에도 누른 방향을 기준으로 한다
		local FacingRad = math.rad(TargetYaw - MeshYawOffset)
		local Dot = (DX * math.cos(FacingRad) + DY * math.sin(FacingRad)) / Distance
		if Dot < math.cos(math.rad(PushHalfAngle)) then
			return
		end

		-- 실제로 밀었을 때만 쿨타임이 돈다. 헛치면 바로 다시 누를 수 있다
		PushCooldownLeft = PushCooldown
		if bTrade then
			bTraded = true
		end

		-- 나에게서 상대 쪽으로 민다
		GameState.PushPlayer(3 - PlayerNumber, DX / Distance, DY / Distance)
		if OnPush then
			OnPush()
		end
	end

	-- 원형 플레이 영역 밖으로 나갔으면 중심 방향으로 원 위까지 되돌린다.
	-- 원을 따라 미끄러지듯 움직인다. 루트가 부모 없는 최상위라 Relative = 월드 위치
	local function ClampToFloor()
		local Location = Root:GetRelativeLocation()
		local DX, DY = Location.X - Floor.CenterX, Location.Y - Floor.CenterY
		local Distance = math.sqrt(DX * DX + DY * DY)
		if Distance > Floor.PlayerRadius then
			local Scale = Floor.PlayerRadius / Distance
			Location.X = Floor.CenterX + DX * Scale
			Location.Y = Floor.CenterY + DY * Scale
			Root:SetRelativeLocation(Location)
		end
	end

	-- 밀침을 받았으면 Knockback·HitSquash 컴포넌트를 시작한다. 실제 이동·찌그러짐은 컴포넌트가 Tick에서 한다.
	-- 밀려나는 중이면 true
	local function UpdateKnockback()
		local Incoming = GameState.TakeKnockback(PlayerNumber)
		if Incoming then
			Knockback:Launch(FVector(Incoming.X, Incoming.Y, 0))
			HitSquash:Squash()
			bTraded = false
			if Config.KnockbackSound then
				Audio.Play(Config.KnockbackSound)
			end
		end
		if not Knockback:IsKnockedBack() then
			return false
		end

		-- 벽 Box가 1차로 막지만 빠르게 밀려나면 넘어갈 수 있어 원형 영역 안으로 가둔다
		ClampToFloor()
		return true
	end

	function Controller.IsMoving()
		return bMoving
	end

	-- PlayerController: GetPlayerController(i), Input: EnableInput(i)
	function Controller.BeginPlay(Owner, PlayerController, Input)
		Actor = Owner
		Root = Owner:GetRootComponent()
		TargetYaw = Root:GetRelativeRotation().Yaw

		-- 어떤 키가 어떤 Axis인지는 PlayerController가 들고 있다. 플레이어마다 컨트롤러가 달라 같은 이름에 다른 키를 쓴다
		PlayerController:AddAxisMapping("MoveForward", Config.Forward, 1.0)
		PlayerController:AddAxisMapping("MoveForward", Config.Back, -1.0)
		PlayerController:AddAxisMapping("MoveRight", Config.Right, 1.0)
		PlayerController:AddAxisMapping("MoveRight", Config.Left, -1.0)

		-- 액터 Tick보다 먼저 불리므로 Tick에서는 이번 프레임 값을 쓴다
		Input:BindAxis("MoveForward", function(Value) AxisForward = Value end)
		Input:BindAxis("MoveRight", function(Value) AxisRight = Value end)

		PlayerController:AddActionMapping("Push", Config.PushKey)
		Input:BindAction("Push", "Pressed", TryPush)

		Knockback = Owner:FindComponentByName("Knockback")
		HitSquash = Owner:FindComponentByName("HitSquash")
		PushCooldownLeft = 0
		Knockback:StopKnockback()
		bTraded = false

		-- 바닥 중앙을 기준으로 좌우에 세운다. 높이(Z)는 씬에 배치한 값을 그대로 쓴다
		if SpawnOffsetY then
			local Location = Root:GetRelativeLocation()
			Location.X = (Floor.MinX + Floor.MaxX) * 0.5
			Location.Y = (Floor.MinY + Floor.MaxY) * 0.5 + SpawnOffsetY
			Root:SetRelativeLocation(Location)

			-- 중앙 쪽(상대 쪽)을 바라보게 한다. 왼쪽(-Y)에 서면 +Y(Yaw 90), 오른쪽이면 -Y(Yaw -90)
			TargetYaw = WrapAngle((SpawnOffsetY < 0 and 90 or -90) + MeshYawOffset)
			local Rotation = Root:GetRelativeRotation()
			Rotation.Yaw = TargetYaw
			Root:SetRelativeRotation(Rotation)
		end
	end

	function Controller.Tick(dt)
		-- 제한 시간 안에서만 움직인다 (시작 대기·결과 화면에서는 멈춤)
		if not GameState.IsPlaying() then
			-- 판이 끝나면 엔딩 시퀀스가 자리를 옮기므로 밀려나던 것을 멈춘다 (시퀀스와 같이 움직이면 떨린다)
			if GameState.Phase == "TimeOver" and Knockback:IsKnockedBack() then
				Knockback:StopKnockback()
			end
			bMoving = false
			return
		end

		PushCooldownLeft = math.max(0, PushCooldownLeft - dt)

		-- 밀려나는 동안은 입력으로 움직이지 않는다
		if UpdateKnockback() then
			bMoving = false
			return
		end

		local DX, DY = AxisForward, AxisRight
		bMoving = DX ~= 0 or DY ~= 0

		if DX ~= 0 or DY ~= 0 then
			-- 대각선으로 눌러도 같은 속도가 되게 정규화한다
			local Len = math.sqrt(DX * DX + DY * DY)
			local MoveX = DX / Len * Speed * dt
			local MoveY = DY / Len * Speed * dt

			-- 축을 나눠 Sweep한다. 한 번에 옮기면 벽에 비스듬히 닿을 때 통째로 멈추지만,
			-- 나눠 옮기면 막힌 축만 멈추고 나머지 축으로는 벽을 따라 미끄러진다
			if MoveX ~= 0 then Actor:AddActorWorldOffset(FVector(MoveX, 0, 0), true) end
			if MoveY ~= 0 then Actor:AddActorWorldOffset(FVector(0, MoveY, 0), true) end
			ClampToFloor()

			-- 엔진은 Yaw 0이 +X, Yaw 90이 +Y (단위는 도)
			TargetYaw = math.deg(Atan2(DY, DX)) + MeshYawOffset
		end

		-- 이동 방향을 바라보게 매 프레임 조금씩 돈다. Alpha를 1 - e^(-k·dt)로 두면 프레임 속도와 무관하다
		local Rotation = Root:GetRelativeRotation()
		local Alpha = 1 - math.exp(-TurnSpeed * dt)
		Rotation.Yaw = WrapAngle(Rotation.Yaw + WrapAngle(TargetYaw - Rotation.Yaw) * Alpha)
		Root:SetRelativeRotation(Rotation)
	end

	function Controller.EndPlay()
		Actor = nil
		Root = nil   -- 참조를 놓아서 삭제된 뒤 접근하지 않게 함
		Knockback = nil
		HitSquash = nil
	end

	return Controller
end
