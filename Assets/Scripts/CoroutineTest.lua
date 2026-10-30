function BeginPlay()
    print("BeginPlay")
    start_coroutine(function()
        print("A1")
        wait_frame()
        print("A2 (다음 프레임)")
        wait(1.0)
        print("A3 (1초 뒤)")
        local n = 0
        wait_until(function() n = n + 1; return n >= 3 end)
        print("A4 (조건 3번째에 통과)")
        start_coroutine(function()
            wait(0.5)
            print("B (코루틴 안에서 시작한 코루틴)")
        end)
    end)

    start_coroutine(function()
        wait(3.0)
        print("Destroy")
        obj:Destroy()
    end)
end

function EndPlay()
    print("EndPlay")
end