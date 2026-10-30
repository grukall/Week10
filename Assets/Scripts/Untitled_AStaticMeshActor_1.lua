local Root
local Speed = 1.0   -- 초당 이동량 (Z 방향)

function BeginPlay()
	Root = obj:GetRootComponent()
	print("[BeginPlay] start:", Root:GetRelativeLocation())
end

function Tick(dt)
	local Location = Root:GetRelativeLocation()
	Location.Z = Location.Z + Speed * dt
	Root:SetRelativeLocation(Location)
end

function EndPlay()
	Root = nil   
	print("[EndPlay] ")
end

function OnOverlap(OtherActor)
end