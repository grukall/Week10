#pragma once
#include <sol/sol.hpp>      

// ScriptComponent가 이 class를 하나씩 가짐. (작업 목록)
// header가 무거우므로 해당 class 선언만 하기 ( class FLuaCoroutine...;)
class FLuaCoroutineScheduler
{
public:
    FLuaCoroutineScheduler() = default;
    FLuaCoroutineScheduler(const FLuaCoroutineScheduler&) = delete;
    FLuaCoroutineScheduler& operator=(const FLuaCoroutineScheduler&) = delete;

    void RegisterLuaAPI(sol::environment& Env);

    int32 Start(sol::function Fn);
    void Stop(int32 Id);
    void StopAll();

    void Tick(float DeltaTime);
    int32 Num() const { return Tasks.Num() + PendingAdd.Num(); }

private:
    enum class EWaitType : uint8 {
        NextFrame,  // 다음 프레임에 바로 깨움
        Time,       // 정해진 시간이 지나면 깨움
        Until       // 조건 함수가 true이면 깨움
    };

    struct FTask
    {
        int32 Id = 0;                                   // Coroutine 고유 번호
        sol::thread Thread;                             // Coroutine 전용 lua_State
        sol::coroutine Routine;                         // Resume 할 대상
        EWaitType WaitType = EWaitType::NextFrame;
        float Remaining = 0.0f;                         // WaitType이 Time일 때 사용
        sol::protected_function Predicate;              // WaitType이 Until일 때 사용
        bool bDead = false;                             // 끝난 Coroutine 이면 true
    };

    void Resume(FTask& Task);       // resume 후 yield 값으로 다음 대기 조건 결정
    TArray<FTask> Tasks;
    TArray<FTask> PendingAdd;       // Tick 도중 시작된 Coroutine 모아두기. Tick 끝나면 한 번에 Tasks에 넣음.

    lua_State* MainState = nullptr;
    int32 NextId = 1;
    bool bTicking = false;
};
