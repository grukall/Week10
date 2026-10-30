local T = 0
function Tick(dt)
    T = T + dt
    if T > 2.0 then
        print("Destroy")
        obj:Destroy()
    end
end
function EndPlay() print("EndPlay") end