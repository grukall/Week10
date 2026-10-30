#pragma once

#include "Core/EngineString.h"
#include "Property.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Color.h"
#include "UObject/Object.h"
#include "UObject/Casts.h"
#include <format>
#include <functional>
#include <tuple>
#include <variant>

// Lua도 C++도 아닌 "엔진 공용 값". 지원 타입은 EPropertyType과 같게 맞춘다
using FScriptValue = std::variant<std::monostate, float, int32, bool, FString, FName,
	FVector, FVector4, FRotator, FTransform, FLinearColor, FColor, UObject*>;

// 함수 지정자 (UE의 UFUNCTION 지정자에 대응)
enum EFunctionFlags : uint32
{
	FUNC_None        = 0,
	FUNC_Const       = 1 << 0, // const 멤버 함수
	FUNC_HasOutParms = 1 << 1, // 출력 매개변수가 하나 이상 있다
};

// 매개변수 지정자 (UE의 CPF_OutParm에 대응)
enum EParamFlags : uint32
{
	PARAM_None = 0,
	PARAM_Out  = 1 << 0, // non-const 참조. 호출 뒤 값을 읽어 돌려준다. const 참조는 일반 입력이다.
};

// 리플렉션에 등록된 멤버 함수. FUNCTION(Name) 매크로가 UClass::AddFunction으로 만든다.
struct FFunction
{
	FString               Name;
	EPropertyType         ReturnType = EPropertyType::Unknown;    // void면 Unknown
	TArray<EPropertyType> ParamTypes;                             // 디버그 출력, 인자 검사용
	TArray<uint32>        ParamFlags;                             // ParamTypes와 같은 순서의 EParamFlags
	uint32                Flags = FUNC_None;

	// 타입을 지운 호출 함수. 실패하면 false와 에러 메시지
	// Args는 매개변수 개수만큼 받는다. 출력 매개변수 자리의 값은 쓰이지 않으므로 비워 두면 된다.
	// OutParams에는 출력 매개변수의 호출 뒤 값이 선언 순서대로 담긴다.
	std::function<bool(UObject* Self, const TArray<FScriptValue>& Args,
		FScriptValue& OutReturn, TArray<FScriptValue>& OutParams, FString& OutError)> Invoke;
};

namespace ReflectionDetail
{
	// FScriptValue → 매개변수 타입. 타입이 다르면 false
	template <typename T>
	bool FromScriptValue(const FScriptValue& In, T& Out)
	{
		if constexpr (std::is_pointer_v<T> && std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
		{
			// nil은 nullptr로 받는다
			if (std::holds_alternative<std::monostate>(In)) { Out = nullptr; return true; }
			UObject* const* Obj = std::get_if<UObject*>(&In);
			if (!Obj) return false;
			Out = *Obj ? Cast<std::remove_pointer_t<T>>(*Obj) : nullptr;
			// 다른 클래스의 객체면 실패
			return *Obj == nullptr || Out != nullptr;
		}
		else if constexpr (std::is_same_v<T, uint32>)
		{
			const int32* V = std::get_if<int32>(&In);
			if (!V) return false;
			Out = static_cast<uint32>(*V);
			return true;
		}
		else
		{
			const T* V = std::get_if<T>(&In);
			if (!V) return false;
			Out = *V;
			return true;
		}
	}

	// 반환값 → FScriptValue. 암시적 변환으로 엉뚱한 칸에 들어가지 않게 칸을 명시한다
	template <typename T>
	FScriptValue ToScriptValue(const T& Value)
	{
		if constexpr (std::is_pointer_v<T> && std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
			return FScriptValue(std::in_place_type<UObject*>, static_cast<UObject*>(Value));
		else if constexpr (std::is_same_v<T, uint32>)
			return FScriptValue(std::in_place_type<int32>, static_cast<int32>(Value));
		else
			return FScriptValue(std::in_place_type<T>, Value);
	}

	// UE의 규칙과 같다. non-const 참조만 출력이고, const 참조는 복사를 피하려는 입력이다.
	template <typename T>
	inline constexpr bool IsOutParam = std::is_lvalue_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>;

	// 인자 검사 → 변환 → 호출 → 반환값과 출력 매개변수 포장
	// Args는 선언 그대로의 매개변수 타입(참조 포함)이다.
	template <typename TClass, typename R, typename... Args, typename TCaller, size_t... I>
	bool Invoke(const TCaller& Caller, UObject* Self, const TArray<FScriptValue>& InArgs,
		FScriptValue& OutRet, TArray<FScriptValue>& OutParams, FString& OutErr, std::index_sequence<I...>)
	{
		if (!Self || !Self->IsA(TClass::StaticClass()))
		{
			OutErr = "호출 대상 객체가 없거나 클래스가 맞지 않습니다";
			return false;
		}
		if (InArgs.Num() != static_cast<int32>(sizeof...(Args)))
		{
			OutErr = std::format("인자 {}개가 필요한데 {}개가 전달됐습니다", sizeof...(Args), InArgs.Num());
			return false;
		}

		// 함수에 실제로 넘겨질 메모리. 출력 매개변수는 기본값으로 두고, 호출 뒤 여기서 결과를 읽는다.
		// UE가 FOutParmRec로 결과를 쓸 주소를 넘기는 것과 같은 역할이다.
		std::tuple<std::decay_t<Args>...> Converted;
		int32 BadIndex = -1;
		// 앞에서부터 변환하다가 실패하면 멈추고 그 번호를 기록한다. 출력 매개변수는 변환하지 않는다.
		const bool bConverted = ((IsOutParam<Args> || FromScriptValue(InArgs[I], std::get<I>(Converted)) || (BadIndex = static_cast<int32>(I), false)) && ...);
		if (!bConverted)
		{
			OutErr = std::format("{}번째 인자의 타입이 맞지 않습니다", BadIndex + 1);
			return false;
		}

		TClass* Obj = static_cast<TClass*>(Self);
		if constexpr (std::is_void_v<R>)
		{
			Caller(Obj, std::get<I>(Converted)...);
			OutRet = FScriptValue();
		}
		else
		{
			OutRet = ToScriptValue<std::decay_t<R>>(Caller(Obj, std::get<I>(Converted)...));
		}

		// 함수가 써 넣은 출력 매개변수 값을 선언 순서대로 모은다
		OutParams.Reset();
		auto CollectOut = [&]<size_t Index>()
		{
			using ArgType = std::tuple_element_t<Index, std::tuple<Args...>>;
			if constexpr (IsOutParam<ArgType>)
				OutParams.Add(ToScriptValue<std::decay_t<ArgType>>(std::get<Index>(Converted)));
		};
		(CollectOut.template operator()<I>(), ...);
		(void)CollectOut;   // 매개변수가 없으면 위 fold가 비어 쓰이지 않는다

		return true;
	}
}
