#include "EnginePCH.h"
#include "Script/LuaReflection.h"
#include "GameFramework/Actor.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

namespace
{
	// 디테일 패널·직렬화와 같이 클래스 계층을 따라 올라가며 찾는다.
	const FProperty* FindProperty(const UObject& Object, const std::string& Key)
	{
		for (UClass* Class = Object.GetClass(); Class; Class = Class->Super)
		{
			for (const FProperty& Property : Class->GetProperties())
			{
				if (Property.Name == Key)
					return &Property;
			}
		}
		return nullptr;
	}

	FString GetClassName(const UObject& Object)
	{
		return Object.GetClass() ? Object.GetClass()->Name : FString("UObject");
	}

	[[noreturn]] void ThrowUnsupported(const UObject& Object, const FProperty& Property)
	{
		throw sol::error(std::format("{}.{} : 이 프로퍼티 타입은 아직 Lua에 바인딩되지 않았습니다", GetClassName(Object), Property.Name));
	}

	void ExpectType(bool bMatches, const UObject& Object, const FProperty& Property, const char* Expected)
	{
		if (!bMatches)
			throw sol::error(std::format("{}.{} : {} 값이 필요합니다", GetClassName(Object), Property.Name, Expected));
	}

	// Lua 값 → FScriptValue. Lua number는 float인지 int인지 모르므로 매개변수 타입을 보고 정한다.
	bool FromLua(const sol::object& Value, EPropertyType Expected, FScriptValue& Out)
	{
		const sol::type LuaType = Value.get_type();
		switch (Expected)
		{
		case EPropertyType::Float:
			if (LuaType != sol::type::number) return false;
			Out = FScriptValue(std::in_place_type<float>, Value.as<float>());
			return true;
		case EPropertyType::Int:
			if (LuaType != sol::type::number) return false;
			Out = FScriptValue(std::in_place_type<int32>, static_cast<int32>(Value.as<double>()));
			return true;
		case EPropertyType::Bool:
			if (LuaType != sol::type::boolean) return false;
			Out = FScriptValue(std::in_place_type<bool>, Value.as<bool>());
			return true;
		case EPropertyType::String:
			if (LuaType != sol::type::string) return false;
			Out = FScriptValue(std::in_place_type<FString>, Value.as<FString>());
			return true;
		case EPropertyType::Name:
			if (LuaType != sol::type::string) return false;
			Out = FScriptValue(std::in_place_type<FName>, FName(Value.as<FString>()));
			return true;
		case EPropertyType::Vector:
			if (!Value.is<FVector>()) return false;
			Out = FScriptValue(std::in_place_type<FVector>, Value.as<FVector>());
			return true;
		case EPropertyType::Rotator:
			if (!Value.is<FRotator>()) return false;
			Out = FScriptValue(std::in_place_type<FRotator>, Value.as<FRotator>());
			return true;
		case EPropertyType::Vector4:
			if (!Value.is<FVector4>()) return false;
			Out = FScriptValue(std::in_place_type<FVector4>, Value.as<FVector4>());
			return true;
		case EPropertyType::Transform:
			if (!Value.is<FTransform>()) return false;
			Out = FScriptValue(std::in_place_type<FTransform>, Value.as<FTransform>());
			return true;
		case EPropertyType::LinearColor:
			if (!Value.is<FLinearColor>()) return false;
			Out = FScriptValue(std::in_place_type<FLinearColor>, Value.as<FLinearColor>());
			return true;
		case EPropertyType::ByteColor:
			if (!Value.is<FColor>()) return false;
			Out = FScriptValue(std::in_place_type<FColor>, Value.as<FColor>());
			return true;
		case EPropertyType::Object:
			// nil은 비워 두면 Invoke가 nullptr로 받는다
			if (LuaType == sol::type::lua_nil) { Out = FScriptValue(); return true; }
			if (!Value.is<FLuaObjectRef>()) return false;
			// 함수를 부르는 지금 살아 있는지 확인하고 주소로 바꾼다 (Destroy 됐으면 Lua Error)
			Out = FScriptValue(std::in_place_type<UObject*>, &Value.as<FLuaObjectRef>().Resolve());
			return true;
		default:
			return false;
		}
	}

	// FScriptValue → Lua 값
	sol::object ToLua(sol::state_view Lua, const FScriptValue& Value)
	{
		return std::visit([&Lua](const auto& V) -> sol::object
		{
			using T = std::decay_t<decltype(V)>;
			if constexpr (std::is_same_v<T, std::monostate>)
				return sol::make_object(Lua, sol::lua_nil);
			else if constexpr (std::is_same_v<T, FName>)
				return sol::make_object(Lua, V.ToString());          // Lua에는 FName이 없으므로 문자열로
			else if constexpr (std::is_same_v<T, UObject*>)
				return LuaReflection::MakeObject(Lua, V);            // 실제 클래스에 맞는 usertype으로
			else
				return sol::make_object(Lua, V);
		}, Value);
	}

	// obj:Func(a, b) 호출을 처리한다. Args[0]은 콜론 호출로 넘어온 self.
	// 출력 매개변수(non-const 참조)는 Lua에서 넘기지 않고, 반환값 뒤에 이어서 다중 반환한다.
	//   C++: bool Trace(FVector Start, FVector End, FVector& OutHit)
	//   Lua: local ok, hit = obj:Trace(a, b)
	sol::variadic_results CallReflectedFunction(const FFunction& Func, sol::variadic_args Args, sol::this_state S)
	{
		sol::state_view Lua(S);

		if (Args.size() == 0 || !Args[0].is<FLuaObjectRef>())
			throw sol::error(std::format("{}() : obj:{}() 처럼 콜론(:)으로 호출해야 합니다", Func.Name, Func.Name));
		// 호출하는 지금 살아 있는지 확인한다 (Destroy 됐으면 Lua Error)
		UObject* Self = &Args[0].as<FLuaObjectRef>().Resolve();
		const FString Where = std::format("{}.{}()", Self ? GetClassName(*Self) : FString("nil"), Func.Name);

		int32 NumInputParams = 0;
		for (uint32 ParamFlag : Func.ParamFlags)
		{
			if (!(ParamFlag & PARAM_Out))
				++NumInputParams;
		}

		const int32 NumLuaArgs = static_cast<int32>(Args.size()) - 1;
		if (NumLuaArgs != NumInputParams)
			throw sol::error(std::format("{} : 인자 {}개가 필요한데 {}개가 전달됐습니다", Where, NumInputParams, NumLuaArgs));

		// Invoke는 매개변수 개수만큼 받는다. 출력 매개변수 자리는 비워 두고, 나머지는 Lua 인자를 순서대로 채운다.
		TArray<FScriptValue> InArgs;
		int32 LuaArgIndex = 1;
		for (int32 i = 0; i < Func.ParamTypes.Num(); ++i)
		{
			if (Func.ParamFlags[i] & PARAM_Out)
			{
				InArgs.Add(FScriptValue());
				continue;
			}

			FScriptValue Converted;
			if (!FromLua(Args.get<sol::object>(LuaArgIndex), Func.ParamTypes[i], Converted))
				throw sol::error(std::format("{} : {}번째 인자의 타입이 맞지 않습니다", Where, LuaArgIndex));
			InArgs.Add(std::move(Converted));
			++LuaArgIndex;
		}

		FScriptValue Result;
		TArray<FScriptValue> OutParams;
		FString Error;
		if (!Func.Invoke(Self, InArgs, Result, OutParams, Error))
			throw sol::error(std::format("{} : {}", Where, Error));

		// 반환값(void가 아니면) → 출력 매개변수들 순서로 돌려준다
		sol::variadic_results Results;
		if (Func.ReturnType != EPropertyType::Unknown)
			Results.push_back(ToLua(Lua, Result));
		for (const FScriptValue& OutParam : OutParams)
			Results.push_back(ToLua(Lua, OutParam));
		return Results;
	}
}

sol::object LuaReflection::MakeObject(sol::state_view Lua, UObject* Object)
{
	// 파괴 예정 객체도 Lua에는 nil로 넘긴다
	if (!::IsValid(Object))
		return sol::make_object(Lua, sol::lua_nil);

	// 액터든 컴포넌트든 같은 핸들 타입으로 넘긴다. 멤버는 리플렉션이 실제 클래스를 보고 찾는다.
	return sol::make_object(Lua, FLuaObjectRef{ FWeakObjectPtr(Object) });
}

sol::object LuaReflection::GetProperty(UObject& Object, const std::string& Key, sol::this_state S)
{
	sol::state_view Lua(S);

	const FProperty* Property = FindProperty(Object, Key);
	if (!Property)
		return sol::make_object(Lua, sol::lua_nil);

	void* Ptr = reinterpret_cast<uint8*>(&Object) + Property->Offset;

	// 값 타입은 복사본으로 넘어간다. obj.Prop.X = 1 은 원본을 바꾸지 않는다.
	switch (Property->Type)
	{
	case EPropertyType::Float:   return sol::make_object(Lua, *static_cast<float*>(Ptr));
	case EPropertyType::Int:     return sol::make_object(Lua, *static_cast<int32*>(Ptr));
	case EPropertyType::Bool:    return sol::make_object(Lua, *static_cast<bool*>(Ptr));
	case EPropertyType::String:  return sol::make_object(Lua, *static_cast<FString*>(Ptr));
	// Lua에는 FName이 없으므로 문자열로 넘긴다
	case EPropertyType::Name:    return sol::make_object(Lua, static_cast<FName*>(Ptr)->ToString());
	case EPropertyType::Vector:  return sol::make_object(Lua, *static_cast<FVector*>(Ptr));
	case EPropertyType::Rotator: return sol::make_object(Lua, *static_cast<FRotator*>(Ptr));
	case EPropertyType::Vector4:     return sol::make_object(Lua, *static_cast<FVector4*>(Ptr));
	case EPropertyType::Transform:   return sol::make_object(Lua, *static_cast<FTransform*>(Ptr));
	case EPropertyType::LinearColor: return sol::make_object(Lua, *static_cast<FLinearColor*>(Ptr));
	case EPropertyType::ByteColor:   return sol::make_object(Lua, *static_cast<FColor*>(Ptr));
	case EPropertyType::Object:  return MakeObject(Lua, *static_cast<UObject**>(Ptr));
	default:
		ThrowUnsupported(Object, *Property);
	}
}

void LuaReflection::SetProperty(UObject& Object, const std::string& Key, sol::stack_object Value)
{
	const FProperty* Property = FindProperty(Object, Key);
	if (!Property)
		throw sol::error(std::format("{} 에 '{}' 프로퍼티가 없습니다", GetClassName(Object), Key));

	if (Property->HasAnyFlags(CPF_EditConst))
		throw sol::error(std::format("{}.{} 은(는) 읽기 전용입니다", GetClassName(Object), Key));

	void* Ptr = reinterpret_cast<uint8*>(&Object) + Property->Offset;

	switch (Property->Type)
	{
	case EPropertyType::Float:
		ExpectType(Value.get_type() == sol::type::number, Object, *Property, "number");
		*static_cast<float*>(Ptr) = Value.as<float>();
		break;
	case EPropertyType::Int:
		ExpectType(Value.get_type() == sol::type::number, Object, *Property, "number");
		*static_cast<int32*>(Ptr) = static_cast<int32>(Value.as<double>());
		break;
	case EPropertyType::Bool:
		ExpectType(Value.get_type() == sol::type::boolean, Object, *Property, "boolean");
		*static_cast<bool*>(Ptr) = Value.as<bool>();
		break;
	case EPropertyType::String:
		ExpectType(Value.get_type() == sol::type::string, Object, *Property, "string");
		*static_cast<FString*>(Ptr) = Value.as<FString>();
		break;
	case EPropertyType::Name:
		ExpectType(Value.get_type() == sol::type::string, Object, *Property, "string");
		*static_cast<FName*>(Ptr) = FName(Value.as<FString>());
		break;
	case EPropertyType::Vector:
		ExpectType(Value.is<FVector>(), Object, *Property, "FVector");
		*static_cast<FVector*>(Ptr) = Value.as<FVector>();
		break;
	case EPropertyType::Rotator:
		ExpectType(Value.is<FRotator>(), Object, *Property, "FRotator");
		*static_cast<FRotator*>(Ptr) = Value.as<FRotator>();
		break;
	case EPropertyType::Vector4:
		ExpectType(Value.is<FVector4>(), Object, *Property, "FVector4");
		*static_cast<FVector4*>(Ptr) = Value.as<FVector4>();
		break;
	case EPropertyType::Transform:
		ExpectType(Value.is<FTransform>(), Object, *Property, "FTransform");
		*static_cast<FTransform*>(Ptr) = Value.as<FTransform>();
		break;
	case EPropertyType::LinearColor:
		ExpectType(Value.is<FLinearColor>(), Object, *Property, "FLinearColor");
		*static_cast<FLinearColor*>(Ptr) = Value.as<FLinearColor>();
		break;
	case EPropertyType::ByteColor:
		ExpectType(Value.is<FColor>(), Object, *Property, "FColor");
		*static_cast<FColor*>(Ptr) = Value.as<FColor>();
		break;
	case EPropertyType::Object:
	{
		UObject* NewObject = nullptr;
		if (Value.get_type() != sol::type::lua_nil)
		{
			ExpectType(Value.is<FLuaObjectRef>(), Object, *Property, "UObject 또는 nil");
			NewObject = &Value.as<FLuaObjectRef>().Resolve();
		}
		// 머티리얼 슬롯에 메시를 넣는 것처럼 선언된 클래스와 다른 객체는 막는다
		if (NewObject && Property->Class)
			ExpectType(NewObject->IsA(Property->Class), Object, *Property, Property->Class->Name.c_str());
		*static_cast<UObject**>(Ptr) = NewObject;
		break;
	}
	default:
		ThrowUnsupported(Object, *Property);
	}

	Object.PostEditChangeProperty(*Property);
}

sol::table LuaReflection::GetClassFunctionTable(sol::state_view Lua, UClass* Class)
{
	// 스크립트가 건드리지 못하도록 전역이 아니라 레지스트리에 둔다. VM과 수명이 같다.
	sol::table Cache = Lua.registry()["UClassFunctionTables"].get_or_create<sol::table>();

	// UClass*를 그대로 넘기면 sol이 매번 새 userdata로 감싸 키가 달라진다. 주소 값(lightuserdata)을 키로 쓴다.
	void* Key = Class;
	if (sol::optional<sol::table> Existing = Cache[Key])
	{
		return *Existing;
	}

	// 이 클래스에 직접 선언된 함수만 담는다. 부모 클래스의 함수는 Index가 Super를 따라 올라가며 찾는다.
	sol::table Table = Lua.create_table();
	for (const FFunction& Func : Class->GetFunctions())
	{
		// FFunction은 클래스 등록 때 만들어진 뒤 바뀌지 않으므로 주소를 붙잡아 둬도 된다.
		const FFunction* FuncPtr = &Func;
		Table.set_function(Func.Name, [FuncPtr](sol::variadic_args Args, sol::this_state S)
		{
			return CallReflectedFunction(*FuncPtr, Args, S);
		});
	}

	Cache[Key] = Table;
	return Table;
}


// __index fallback: 명시적 멤버 → 클래스별 함수(부모까지) → 프로퍼티 순서로 찾는다
sol::object LuaReflection::Index(UObject& Object, const std::string& Key, sol::this_state S)
{
	sol::state_view Lua(S);

	// UClass*를 거쳐야 해서 FUNCTION으로 등록할 수 없는 값
	if (Key == "ClassName")
		return sol::make_object(Lua, Object.GetClass() ? Object.GetClass()->Name : FString());

	// TArray를 반환해 FUNCTION으로 등록할 수 없으므로 직접 둔다. Lua 배열(1부터)로 돌려준다.
	if (Key == "GetComponents" && Cast<AActor>(&Object))
	{
		return sol::make_object(Lua, [](const FLuaObjectRef& Self, sol::this_state S) {
			sol::state_view L(S);
			sol::table Result = L.create_table();
			if (AActor* Actor = Cast<AActor>(&Self.Resolve()))
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (::IsValid(Component))
						Result.add(MakeObject(L, Component));
				}
			}
			return Result;
		});
	}

	for (UClass* Class = Object.GetClass(); Class; Class = Class->Super)
	{
		sol::object Func = GetClassFunctionTable(Lua, Class)[Key];
		if (Func.valid() && Func.get_type() != sol::type::lua_nil)
			return Func;
	}
	return LuaReflection::GetProperty(Object, Key, S);   // 지금 있는 프로퍼티 탐색
}