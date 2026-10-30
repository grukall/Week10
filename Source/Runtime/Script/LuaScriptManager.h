// LuaScriptManager.h: 헤더에는 sol.hpp를 넣지 않는다
#pragma once
#include <memory>
#include <sol/forward.hpp>

class FLuaScriptManager
{
public:
	static FLuaScriptManager& Get();
	~FLuaScriptManager();

	void Initialize();
	void Shutdown();
	sol::state& GetState();

private:
	std::unique_ptr<sol::state> Lua;
};