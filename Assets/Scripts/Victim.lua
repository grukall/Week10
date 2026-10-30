-- 1초 뒤 스스로 Destroy한다. 다른 스크립트가 참조할 수 있게 _G에 자신을 올려 둔다.
function BeginPlay()
    _G.Victim = obj
    _G.VictimRoot = obj:GetRootComponent()
    print("[Victim] BeginPlay", obj)

    start_coroutine(function()
        wait(1.0)
        local Self = obj
        print("[Victim] Destroy")
        obj:Destroy()

        -- 같은 프레임: 메모리는 남아 있지만 이미 무효
        print("[Victim] same frame IsValid:", IsValid(Self))
        print("[Victim] same frame call:", pcall(function() return Self:GetActorLocation() end))

        -- EndPlay에서 이 코루틴도 정지되므로 다음 yield 이후는 실행되지 않아야 한다
        wait(0.5)
        print("[Victim] [FAIL] this line must not run")
    end)
end

function EndPlay()
    print("[Victim] EndPlay")
end