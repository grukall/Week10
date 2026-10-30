#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Script/LuaReflection.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"

void RegisterLuaObjectBindings(sol::state& Lua)
{
	// 모든 엔진 객체(액터·컴포넌트)는 이 핸들로 넘어간다.
	// 멤버는 전부 리플렉션(FUNCTION, PROPERTY)에서 찾고, 접근할 때마다 대상이 살아 있는지 확인한다.
	Lua.new_usertype<FLuaObjectRef>("UObject",
		sol::no_constructor,

		sol::meta_function::index, [](const FLuaObjectRef& Ref, const std::string& Key, sol::this_state S) {
			return LuaReflection::Index(Ref.Resolve(), Key, S);
		},
		sol::meta_function::new_index, [](const FLuaObjectRef& Ref, const std::string& Key, sol::stack_object Value) {
			LuaReflection::SetProperty(Ref.Resolve(), Key, Value);
		},

		// 핸들은 넘길 때마다 새로 만들어지므로, 같은 객체인지는 UUID로 비교한다 (a == b)
		sol::meta_function::equal_to, [](const FLuaObjectRef& A, const FLuaObjectRef& B) {
			return A.Weak == B.Weak;
		},

		// print(obj) → "AActor(Cube_1)" / 파괴됐으면 "<destroyed>"
		sol::meta_function::to_string, [](const FLuaObjectRef& Ref) {
			UObject* Object = Ref.Weak.Get();
			if (!Object)
				return FString("<destroyed>");
			return std::format("{}({})", Object->GetClass() ? Object->GetClass()->Name : FString("UObject"), Object->GetName());
		}
	);

	// 파괴됐거나 nil이면 false. UE 블루프린트의 IsValid와 같다.
	Lua.set_function("IsValid", [](sol::object Value) {
		return Value.is<FLuaObjectRef>() && Value.as<FLuaObjectRef>().Weak.IsValid();
	});

	// FindStaticMesh("Models/Garlic/garlic.obj") → 메시 핸들, 없으면 nil.
	// 씬 파일의 "StaticMesh" 값과 같은 키를 쓴다. 엔진 시작 때 스캔한 에셋에서 찾기만 하고 새로 로드하지 않는다.
	Lua.set_function("FindStaticMesh", [](const FString& Path, sol::this_state S) {
		return LuaReflection::MakeObject(S, UAssetManager::GetAssetByPath<UStaticMesh>(Path));
	});
}