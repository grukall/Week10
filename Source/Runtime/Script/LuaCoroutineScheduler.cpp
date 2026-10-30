#include "EnginePCH.h"
#include "Script/LuaCoroutineScheduler.h"

// Lua Script에서 사용할 함수 연결 ( start, stop, stopAll, wait ... )
void FLuaCoroutineScheduler::RegisterLuaAPI(sol::environment& Env)
{
    MainState = Env.lua_state();

    // Env Table에 {Lua 함수, C++ 람다} 등록
    Env.set_function("start_coroutine", [this](sol::function Fn) { return Start(Fn); });
    Env.set_function("stop_coroutine", [this](int32 Id) { Stop(Id); });
    Env.set_function("stop_all_coroutines", [this]() { StopAll(); });

    // yield는 Coroutine 안에서 가장 간단해서 Lua 로 작성함.
    sol::state_view L(MainState);
    L.safe_script(R"(
        function wait(seconds)  return coroutine.yield(seconds) end
        function wait_until(fn) return coroutine.yield(fn) end
        function wait_frame() return coroutine.yield() end
        )", Env, sol::script_pass_on_error);
}

// Lua 에서 start_coroutine(...) 호출 시 실행
int32 FLuaCoroutineScheduler::Start(sol::function Fn)
{
    FTask Task;
    const int32 Id = NextId++;      // 나중에 std::move 하므로 미리 지역변수로 저장
    
    Task.Id = Id;
    // New Thread 생성 ( 전역 변수, 메모리는 Main 과 공유, 호출 스택만 별도  )
    Task.Thread = sol::thread::create(MainState);
    // Fn 을 방금 만든 New Thread 에 두고 Coroutine 객체 생성
    Task.Routine = sol::coroutine(Task.Thread.thread_state(), Fn);

    // Coroutine 한 번 Resume 해보기
    Resume(Task);
    if (Task.bDead) return Id;

    // Tick 끝에서 Tasks로 합쳐져 다음 Tick 부터 순회
    PendingAdd.Add(std::move(Task));
    return Id;
}

// Coroutine 실행
void FLuaCoroutineScheduler::Resume(FTask& Task)
{
    // sol2가 내부에서 lua_resume을 호출
    // Result 에는 Lua 함수의 결과값이 들어 있음. ( Wait / Dead / Error )
    sol::protected_function_result Result = Task.Routine();

    // Coroutine 에러 나면 Log 남기고 bDead 처리
    if (!Result.valid())
    {
        sol::error Err = Result;
        HTR_LOG(Error, "[Lua][Coroutine {}] {}", Task.Id, Err.what());
        Task.bDead = true;
        return;
    }
    
    // Coroutine 실행 완료
    if (Result.status() != sol::call_status::yielded)
    {
        Task.bDead = true;
        return;
    }

    // 이전 wait_untile 조건 함수 참조 해제
	Task.Predicate = sol::lua_nil;

    // return_count() 는 yield가 넘긴 값의 개수
    // 0 이면 wait_frame, 그 이상이면 wait이거나 wait_until)
	sol::object Value;
    sol::type ValueType = sol::type::lua_nil;

    if (Result.return_count() > 0)
    {
		Value = Result.get<sol::object>(0);
        ValueType = Value.get_type();
    }

	switch (ValueType)
	{
    // wait
	case sol::type::number:
		Task.WaitType = EWaitType::Time;
		Task.Remaining = Value.as<float>();
		break;

    // wait_until
	case sol::type::function:
		Task.WaitType = EWaitType::Until;
		Task.Predicate = sol::protected_function(MainState, Value);
		break;
    
    // wait_frame
	case sol::type::lua_nil:
		Task.WaitType = EWaitType::NextFrame;
		break;
    
    // 그 외 경로는 Warning Log 출력 후 NextFrame 처리
	default:
		HTR_LOG(Warning, "[Lua][Coroutine {}] unsupported yield value, waiting one frame", Task.Id);
		Task.WaitType = EWaitType::NextFrame;
		break;
	}
}

// Lua 에서 stop_coroutine(id) 호출 시 실행 ( Coroutine[ID] 하나 멈춤 )
// Tick 도중이므로 
void FLuaCoroutineScheduler::Stop(int32 Id)
{
    for (FTask& Task : Tasks)
    {
        if (Task.Id == Id)
            Task.bDead = true;
    }

    for (FTask& Task : PendingAdd)
    {
        if (Task.Id == Id)
            Task.bDead = true;
    }
}

// Lua 에서 stop_all_coroutines(id) 호출 시 실행 ( Scheduler의 모든 Coroutines 멈춤 )
// Tick 바깥 (ScriptComponent) 에서 호출되므로 sol 객체 즉시 해제
void FLuaCoroutineScheduler::StopAll()
{
    for (FTask& Task : Tasks)
        Task.bDead = true;

    for (FTask& Task : PendingAdd)
        Task.bDead = true;

    if (!bTicking)
    {
        Tasks.SetNum(0);
        PendingAdd.SetNum(0);
    }
}

void FLuaCoroutineScheduler::Tick(float DeltaTime)
{
	bTicking = true;

    // Coroutines 목록 순회
	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		FTask& Task = Tasks[i];
		if (Task.bDead) continue;

		switch (Task.WaitType)
		{
        // wait
		case EWaitType::Time:
			Task.Remaining -= DeltaTime;
            // 시간이 남았으면 계속 진행 ( 다음 Coroutine )
			if (Task.Remaining > 0.0f) continue;
            // 시간이 되었으면 종료
			break;

        // wait_until
		case EWaitType::Until:
		{
			sol::protected_function_result Check = Task.Predicate();

            // Error Log 출력 후 종료
			if (!Check.valid())
			{
				sol::error Err = Check;
				HTR_LOG(Error, "[Lua][Coroutine {}] wait_until: {}", Task.Id, Err.what());
				Task.bDead = true;
				continue;
			}

            // 조건 충족이 안 되었으면 계속 진행
			if (!Check.get<bool>())
				continue;
			break;
		}

        // wait_frame
		case EWaitType::NextFrame:
			break;
		}

        // 깨우기 ( Lua 함수 다시 실행 )
		Resume(Task);
	}

    // Coroutine 순회 종료
	bTicking = false;

    // RemoveAt 은 i 뒤의 원소들이 한 칸씩 앞으로 당겨지기 때문에, 뒤에서부터 검사
	for (int32 i = Tasks.Num() - 1; i >= 0; --i)
	{
		if (Tasks[i].bDead)
			Tasks.RemoveAt(i);
	}

    // Tick 도중 추가된 새로운 Coroutines를 Tasks에 넣어주기
	for (FTask& Task : PendingAdd)
	{
		if (!Task.bDead)
			Tasks.Add(std::move(Task));
	}
	PendingAdd.SetNum(0);
}