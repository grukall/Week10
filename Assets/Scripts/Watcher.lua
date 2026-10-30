-- pcall로 감싸서 결과(성공 여부, 에러 메시지)를 출력한다
local function Check(Name, Fn)
    local Ok, Result = pcall(Fn)
    print(string.format("[Watcher] %-30s ok=%-5s %s", Name, tostring(Ok), tostring(Result)))
end

function BeginPlay()
    start_coroutine(function()
        -- 살아 있는 Victim이 올라올 때까지 대기 (이전 PIE에서 남은 핸들은 IsValid가 false라 걸러진다)
        wait_until(function() return IsValid(_G.Victim) end)
        local V, R = _G.Victim, _G.VictimRoot
        print("[Watcher] got victim:", V, "IsValid:", IsValid(V))

        -- Victim이 1초에 Destroy되고 그 프레임 끝에 delete된 뒤, 여러 프레임이 지난 시점
        wait(2.0)
        print("[Watcher] ---- after destroy + delete ----")
        print("[Watcher] IsValid(actor):", IsValid(V), " IsValid(component):", IsValid(R))
        print("[Watcher] tostring:", tostring(V), tostring(R))

        Check("method      V:GetActorLocation()", function() return V:GetActorLocation() end)
        Check("prop read   V.ScriptPath",         function() return V.ScriptPath end)
        Check("prop write  V.ScriptPath = 'x'",   function() V.ScriptPath = "x" end)
        Check("special     V.ClassName",          function() return V.ClassName end)
        Check("special     V:GetComponents()",    function() return V:GetComponents() end)
        Check("component   R.ClassName",          function() return R.ClassName end)

        -- pcall 없이 접근: 이 코루틴만 에러로 끝나고 엔진은 계속 돌아야 한다
        print("[Watcher] ---- without pcall ----")
        local Loc = V:GetActorLocation()
        print("[Watcher] [FAIL] this line must not run", Loc)
    end)
end

-- 위 코루틴이 에러로 끝난 뒤에도 엔진과 이 스크립트가 살아 있는지 확인
local Elapsed = 0
local bPrinted = false
function Tick(dt)
    Elapsed = Elapsed + dt
    if Elapsed > 4.0 and not bPrinted then
        bPrinted = true
        print("[Watcher] Tick still running -> engine alive")
    end
end