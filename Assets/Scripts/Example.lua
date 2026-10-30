local Light

function BeginPlay()
    Light = obj:GetLightComponent()
    print("Intensity:", Light:GetIntensity(), "Color:", Light:GetLightColor())
end

local Time = 0
function Tick(dt)
    Time = Time + dt
    Light:SetIntensity(1.0 + math.sin(Time * 2.0) * 5)   -- 0.5 ~ 1.5 사이로 깜빡임
    -- print("Tick: ", Time, "Intensity", Light:GetIntensity())

end

function EndPlay()
    Light = nil   -- 컴포넌트 참조를 놓아서 삭제된 뒤 접근하지 않게 함
end