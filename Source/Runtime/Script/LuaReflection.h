#pragma once
#include <string>
#include <sol/sol.hpp>
#include "UObject/WeakObjectPtr.h"

class UObject;
class UClass;

// 리플렉션(PROPERTY, FUNCTION)에 등록된 멤버를 Lua에서 이름으로 읽고 쓰고 호출한다.
// usertype의 __index/__newindex fallback으로 쓰므로, 명시적으로 바인딩된 멤버가 항상 먼저 찾아진다.
namespace LuaReflection
{
	// __index fallback. 클래스별 함수(부모까지) → 프로퍼티 순서로 찾는다.
	sol::object Index(UObject& Object, const std::string& Key, sol::this_state S);

	// 등록된 프로퍼티가 없으면 nil을 돌려준다(Lua 관례). 지원하지 않는 타입이면 Lua 에러.
	sol::object GetProperty(UObject& Object, const std::string& Key, sol::this_state S);

	// 없는 프로퍼티, 읽기 전용(CPF_EditConst), 타입이 맞지 않는 값이면 Lua 에러.
	// 디테일 패널과 같이 쓰고 나서 PostEditChangeProperty를 호출한다.
	void SetProperty(UObject& Object, const std::string& Key, sol::stack_object Value);

	// FUNCTION()으로 등록된 함수를 Lua 함수로 감싼 테이블. 클래스당 한 번 만들어 레지스트리에 캐시한다.
	// 이 클래스에 직접 선언된 함수만 담고, 부모 클래스의 함수는 Index가 따라 올라가며 찾는다.
	sol::table GetClassFunctionTable(sol::state_view Lua, UClass* Class);

	// 객체를 Lua 핸들(FLuaObjectRef)로 감싸 넘긴다. null이거나 Destroy 예정이면 nil.
	sol::object MakeObject(sol::state_view Lua, UObject* Object);
}

// Lua에 넘기는 객체 참조. 원시 포인터 대신 약한 참조(UUID)를 담는다.
// 대상이 파괴되면 접근하는 순간 크래시 대신 Lua 에러가 난다.
struct FLuaObjectRef
{
	FWeakObjectPtr Weak;

	// 살아 있으면 객체를 돌려주고, 파괴됐으면 Lua 에러를 낸다.
	UObject& Resolve() const
	{
		if (UObject* Object = Weak.Get())
			return *Object;
		throw sol::error("Object has already been destroyed. Check IsValid(obj) first.");
	}
};