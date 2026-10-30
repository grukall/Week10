// LuaScriptManager.cpp
// (다른 cpp들처럼 첫 줄에 PCH include)
#include "EnginePCH.h"
#include "Script/LuaScriptManager.h"
#include "Script/LuaBindings.h"
#include <sol/sol.hpp>

FLuaScriptManager& FLuaScriptManager::Get()
{
	static FLuaScriptManager Instance;
	return Instance;
}

FLuaScriptManager::~FLuaScriptManager() = default;   // sol::state가 완전한 타입인 cpp에서 정의

sol::state& FLuaScriptManager::GetState()
{
	// Initialize 전이나 Shutdown 후에 Script Load 하려고 할 때
	assert(Lua && "Lua VM is not initialized or already shut down");
	return *Lua;
}

void FLuaScriptManager::Initialize()
{
	Lua = std::make_unique<sol::state>();
	Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
		sol::lib::table, sol::lib::coroutine, sol::lib::package, sol::lib::jit);

	// 스크립트가 로드되기 전에 VM에 한 번만 등록한다
	RegisterLuaBindings(*Lua);

	// Lua의 print를 엔진 로그로 연결 (variadic_args 연습도 겸함)
	Lua->set_function("print", [](sol::this_state S, sol::variadic_args Args) {
		sol::state_view L(S);
		sol::function ToString = L["tostring"];
		std::string Out;
		for (auto Arg : Args) Out += ToString(Arg).get<std::string>() + " ";
		HTR_LOG(Info, "[Lua] {}", Out);
		});

	auto Result = Lua->safe_script(R"(print("Hello from Lua", _VERSION, 1 + 2))",
		sol::script_pass_on_error);
	if (!Result.valid()) {
		sol::error Err = Result;
		HTR_LOG(Error, "[Lua] {}", Err.what());
	}
}

void FLuaScriptManager::Shutdown()
{ 
	Lua.reset(); 
}