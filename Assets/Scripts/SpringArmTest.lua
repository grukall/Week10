-- SpringArm 확인용 피벗 (Assets/Scenes/SpringArmTest.scene). 게임에는 쓰지 않는다.
-- W/S = 앞/뒤, A/D = 왼쪽/오른쪽, Q/E = 왼쪽/오른쪽으로 돌기
-- 화면은 시작하자마자 이 액터의 카메라(SpringArm 끝)로 1초 동안 블렌드해서 넘어온다.

local MoveSpeed = 4.0     -- 초당 이동 거리
local TurnSpeed = 120.0   -- 초당 회전 각도

local AxisForward, AxisRight, AxisTurn = 0, 0, 0
local Root

function BeginPlay()
	Root = obj:GetRootComponent()

	local PC = GetPlayerController(0)
	PC:AddAxisMapping("ArmForward", "W", 1.0)
	PC:AddAxisMapping("ArmForward", "S", -1.0)
	PC:AddAxisMapping("ArmRight", "D", 1.0)
	PC:AddAxisMapping("ArmRight", "A", -1.0)
	PC:AddAxisMapping("ArmTurn", "E", 1.0)
	PC:AddAxisMapping("ArmTurn", "Q", -1.0)

	local Input = EnableInput(0)
	Input:BindAxis("ArmForward", function(Value) AxisForward = Value end)
	Input:BindAxis("ArmRight", function(Value) AxisRight = Value end)
	Input:BindAxis("ArmTurn", function(Value) AxisTurn = Value end)

	SetViewTarget(obj, 1.0)
end

function Tick(dt)
	local Rotation = Root:GetRelativeRotation()
	if AxisTurn ~= 0 then
		Rotation.Yaw = Rotation.Yaw + AxisTurn * TurnSpeed * dt
		Root:SetRelativeRotation(Rotation)
	end

	-- 바라보는 방향 기준으로 움직인다. Sweep(true)이라 Body 박스가 벽 앞에서 멈춘다
	local Yaw = math.rad(Rotation.Yaw)
	local FX, FY = math.cos(Yaw), math.sin(Yaw)
	local Step = MoveSpeed * dt
	local DX = (FX * AxisForward - FY * AxisRight) * Step
	local DY = (FY * AxisForward + FX * AxisRight) * Step
	if DX ~= 0 or DY ~= 0 then
		obj:AddActorWorldOffset(FVector(DX, DY, 0), true)
	end
end
