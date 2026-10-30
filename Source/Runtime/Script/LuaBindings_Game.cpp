#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Engine/Engine.h"
#include <sol/sol.hpp>
#include <cmath>

namespace
{
	// 스크립트가 쓸 수 있는 파일은 이것 하나다. 경로는 다른 엔진 파일처럼 실행 위치(프로젝트 루트) 기준.
	constexpr const char* ScoreboardPath = "Saved/Scoreboard.json";

	// 자기 자신을 담은 테이블이 무한히 돌지 않게 막는다
	constexpr int32 MaxTableDepth = 16;

	json LuaToJson(const sol::object& Value, int32 Depth);

	// 키가 1..n으로 빈칸 없이 이어지면 배열, 아니면 객체로 본다. 빈 테이블은 배열.
	bool IsLuaArray(const sol::table& Table, size_t& OutCount)
	{
		OutCount = 0;
		size_t MaxIndex = 0;
		for (const auto& [Key, Unused] : Table)
		{
			if (Key.get_type() != sol::type::number)
				return false;
			const double Index = Key.as<double>();
			if (Index < 1.0 || Index != std::floor(Index))
				return false;
			MaxIndex = std::max(MaxIndex, static_cast<size_t>(Index));
			++OutCount;
		}
		return MaxIndex == OutCount;
	}

	json LuaTableToJson(const sol::table& Table, int32 Depth)
	{
		if (Depth > MaxTableDepth)
			throw sol::error("SaveScoreboard : 테이블이 너무 깊거나 자기 자신을 담고 있습니다");

		size_t Count = 0;
		if (IsLuaArray(Table, Count))
		{
			json Array = json::array();
			for (size_t i = 1; i <= Count; ++i)
				Array.push_back(LuaToJson(Table.get<sol::object>(i), Depth + 1));
			return Array;
		}

		json Object = json::object();
		for (const auto& [Key, Item] : Table)
		{
			// JSON 객체 키는 문자열뿐이라 숫자 키는 문자열로 바꾼다
			FString KeyString;
			if (Key.get_type() == sol::type::string)
				KeyString = Key.as<FString>();
			else if (Key.get_type() == sol::type::number)
				KeyString = std::format("{}", Key.as<double>());
			else
				throw sol::error("SaveScoreboard : 테이블 키는 문자열이나 숫자여야 합니다");

			Object[KeyString] = LuaToJson(Item, Depth + 1);
		}
		return Object;
	}

	json LuaToJson(const sol::object& Value, int32 Depth)
	{
		switch (Value.get_type())
		{
		case sol::type::lua_nil:
			return nullptr;
		case sol::type::boolean:
			return Value.as<bool>();
		case sol::type::number:
		{
			// LuaJIT는 숫자가 모두 double이다. 정수 값은 정수로 적어야 파일에서 3.0이 아니라 3으로 보인다
			const double Number = Value.as<double>();
			if (Number == std::floor(Number) && std::abs(Number) < 9.0e15)
				return static_cast<int64>(Number);
			return Number;
		}
		case sol::type::string:
			return Value.as<FString>();
		case sol::type::table:
			return LuaTableToJson(Value.as<sol::table>(), Depth);
		default:
			throw sol::error("SaveScoreboard : 숫자, 문자열, bool, 테이블만 저장할 수 있습니다");
		}
	}

	sol::object JsonToLua(sol::state_view Lua, const json& Value)
	{
		switch (Value.type())
		{
		case json::value_t::boolean:
			return sol::make_object(Lua, Value.get<bool>());
		case json::value_t::number_integer:
		case json::value_t::number_unsigned:
		case json::value_t::number_float:
			return sol::make_object(Lua, Value.get<double>());
		case json::value_t::string:
			return sol::make_object(Lua, Value.get<FString>());
		case json::value_t::array:
		{
			sol::table Table = Lua.create_table(static_cast<int>(Value.size()), 0);
			int Index = 1;
			for (const json& Item : Value)
				Table[Index++] = JsonToLua(Lua, Item);
			return Table;
		}
		case json::value_t::object:
		{
			sol::table Table = Lua.create_table();
			for (const auto& [Key, Item] : Value.items())
				Table[Key] = JsonToLua(Lua, Item);
			return Table;
		}
		default:
			return sol::make_object(Lua, sol::lua_nil);
		}
	}
}

void RegisterLuaGameBindings(sol::state& Lua)
{
	// OpenScene("Assets/Scenes/GameJamMap0.scene")
	// 요청만 해 두고 실제 전환은 다음 프레임 시작에 엔진이 한다. 호출한 스크립트는 이번 프레임을 끝까지 돈다.
	// 게임은 게임 월드를, 에디터는 PIE 세션을 유지한 채 PlayWorld만 바꾼다.
	Lua.set_function("OpenScene", [](const FString& ScenePath)
		{
			if (GEngine)
				GEngine->TravelToScene(ScenePath);
		});

	// QuitGame(): 게임은 앱을 끝내고, 에디터 PIE에서는 PIE 세션만 끝낸다.
	Lua.set_function("QuitGame", []()
		{
			if (GEngine)
				GEngine->QuitGame();
		});

	// SaveScoreboard({ { Name = "P1", Score = 120 }, ... }) -> 성공하면 true
	// 숫자·문자열·bool·테이블만 담을 수 있다. 정렬이나 개수 제한은 스크립트가 한다.
	Lua.set_function("SaveScoreboard", [](const sol::table& Scoreboard) -> bool
		{
			const json Root = LuaTableToJson(Scoreboard, 0);

			std::error_code Error;
			fs::create_directories(fs::path(ScoreboardPath).parent_path(), Error);

			// 쓰다가 꺼져도 기존 기록이 깨지지 않게 임시 파일에 다 쓴 뒤 바꿔 끼운다
			const FString TempPath = FString(ScoreboardPath) + ".tmp";
			{
				std::ofstream Out(TempPath);
				if (!Out.is_open())
				{
					HTR_LOG(Error, "[Scoreboard] failed to write: {}", TempPath);
					return false;
				}
				Out << Root.dump(2) << '\n';
			}

			fs::rename(TempPath, ScoreboardPath, Error);
			if (Error)
			{
				HTR_LOG(Error, "[Scoreboard] failed to replace {}: {}", ScoreboardPath, Error.message());
				return false;
			}
			return true;
		});

	// local Scoreboard = LoadScoreboard()
	// 저장한 테이블을 그대로 돌려준다. 파일이 없거나 깨졌으면 빈 테이블.
	Lua.set_function("LoadScoreboard", [](sol::this_state S) -> sol::object
		{
			sol::state_view LuaView(S);

			std::ifstream File(ScoreboardPath);
			if (!File.is_open())
				return LuaView.create_table();

			const json Root = json::parse(File, nullptr, false);
			if (Root.is_discarded() || (!Root.is_array() && !Root.is_object()))
			{
				HTR_LOG(Warning, "[Scoreboard] invalid json: {}", ScoreboardPath);
				return LuaView.create_table();
			}
			return JsonToLua(LuaView, Root);
		});
}
