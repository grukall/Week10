#pragma once

#include "Property.h"
#include "Object.h"
#include "Function.h"

using ClassConstructor = UObject * (*)(void* Memory);

class UClass : public UObject
{
public:
	UClass();

	FString Name;
	UClass* Super = nullptr;
	uint64 ClassSize = 0;
	ClassConstructor Constructor = nullptr;

	TArray<FProperty> Properties;
	TArray<FFunction> Functions;
	UObject* DefaultObject = nullptr;

	bool IsChildOf(const UClass* BaseClass) const;
	UObject* GetDefaultObject();

	inline const TArray<FProperty>& GetProperties() const { return Properties; }

	template<class T>
	T* GetDefaultObject()
	{
		return CastChecked<T>(GetDefaultObject());
	}

	template <typename T>
	void AddProperty(const FString& InName, uint64 InOffset, uint32 InFlags = CPF_Default)
	{
		UClass* ObjClass = nullptr;
		if constexpr (std::is_pointer_v<T> &&
			std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
		{
			ObjClass = std::remove_pointer_t<T>::StaticClass();
		}

		Properties.Add({ InName, GetPropertyType<T>(), InOffset, sizeof(T), ObjClass, InFlags });
	}

	template <typename T>
	void AddProperty(const FString& InName, uint64 InOffset, EPropertyType InType, uint32 InFlags = CPF_Default)
	{
		Properties.Add({ InName, InType, InOffset, sizeof(T), nullptr, InFlags });
	}

	// FUNCTION(Name) 매크로가 호출한다. 일반 멤버 함수와 const 멤버 함수를 같은 구현으로 넘긴다.
	template <typename TClass, typename R, typename... Args>
	void AddFunction(const FString& InName, R (TClass::*Func)(Args...))
	{
		AddFunctionImpl<TClass, R, Args...>(InName,
			[Func](TClass* Obj, auto&... A) -> R { return (Obj->*Func)(A...); }, FUNC_None);
	}

	template <typename TClass, typename R, typename... Args>
	void AddFunction(const FString& InName, R (TClass::*Func)(Args...) const)
	{
		AddFunctionImpl<TClass, R, Args...>(InName,
			[Func](TClass* Obj, auto&... A) -> R { return (Obj->*Func)(A...); }, FUNC_Const);
	}

	inline const TArray<FFunction>& GetFunctions() const { return Functions; }

	// 부모 클래스까지 올라가며 이름으로 찾는다. 없으면 nullptr.
	const FFunction* FindFunction(const FString& InName) const
	{
		for (const UClass* Class = this; Class; Class = Class->Super)
		{
			for (const FFunction& Function : Class->Functions)
			{
				if (Function.Name == InName)
					return &Function;
			}
		}
		return nullptr;
	}

private:
	template <typename TClass, typename R, typename... Args, typename TCaller>
	void AddFunctionImpl(const FString& InName, TCaller Caller, uint32 InFlags)
	{
		FFunction F;
		F.Name  = InName;
		F.Flags = InFlags;

		if constexpr (std::is_void_v<R>)
			F.ReturnType = EPropertyType::Unknown;
		else
			F.ReturnType = GetPropertyType<std::decay_t<R>>();

		// 기존 PROPERTY의 타입 매핑을 재사용한다. 지원하지 않는 타입이면 static_assert로 막힌다.
		// 인자가 0개여도 동작하도록 fold로 하나씩 추가한다.
		(F.ParamTypes.Add(GetPropertyType<std::decay_t<Args>>()), ...);
		// non-const 참조는 출력 매개변수로 표시한다 (UE의 CPF_OutParm)
		(F.ParamFlags.Add(ReflectionDetail::IsOutParam<Args> ? PARAM_Out : PARAM_None), ...);
		if constexpr ((ReflectionDetail::IsOutParam<Args> || ...))
			F.Flags |= FUNC_HasOutParms;

		// 참조 여부를 알아야 하므로 decay하지 않은 매개변수 타입을 그대로 넘긴다
		F.Invoke = [Caller](UObject* Self, const TArray<FScriptValue>& InArgs, FScriptValue& OutRet, TArray<FScriptValue>& OutParams, FString& OutErr)
		{
			return ReflectionDetail::Invoke<TClass, R, Args...>(
				Caller, Self, InArgs, OutRet, OutParams, OutErr, std::index_sequence_for<Args...>{});
		};
		Functions.Add(std::move(F));
	}


	mutable int32 ClassUnique = 0;
	friend FName MakeUniqueObjectName(UObject*, const UClass*, FName);
};

//보류
inline void CopyProperties(UObject* Src, UObject* Dst, UClass* FromClass)
{
	for (UClass* c = FromClass; c; c = c->Super)
		for (const FProperty& p : c->Properties)
			memcpy((char*)Dst + p.Offset, (char*)Src + p.Offset, p.Size);
}