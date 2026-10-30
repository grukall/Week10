#pragma once

#include "UObject/Object.h"
#include "UObject/Casts.h"

// UUID로 객체를 가리키는 약한 참조
// 대상이 delete 됐거나 Destroy 예정이면 Get()이 return nullptr.
class FWeakObjectPtr
{
public:
	FWeakObjectPtr() = default;
	FWeakObjectPtr(const UObject* Object) : UUID(Object ? Object->GetUUID() : 0) {}

	// 살아 있으면 return Object, 아니면 nullptr
	UObject* Get() const
	{
		UObject* Object = FindObjectByUUID(UUID);
		// 클래스 안에 같은 이름의 IsValid()가 있으므로 전역 함수는 ::로 부른다
		return ::IsValid(Object) ? Object : nullptr;
	}

	bool IsValid() const { return Get() != nullptr; }
	uint32 GetUUID() const { return UUID; }
	void Reset() { UUID = 0; }

	// 같은 객체를 가리키는지 (살아 있는지와는 별개)
	bool operator==(const FWeakObjectPtr& Other) const { return UUID == Other.UUID; }

private:
	uint32 UUID = 0;
};

// 타입이 정해진 약한 참조. Get()이 T*를 돌려준다.
template <typename T>
class TWeakObjectPtr : public FWeakObjectPtr
{
public:
	using FWeakObjectPtr::FWeakObjectPtr;

	T* Get() const { return Cast<T>(FWeakObjectPtr::Get()); }
	T* operator->() const { return Get(); }
    
    // if (Obj) 로 살아있는지 바로 확인할 수 있게
	explicit operator bool() const { return IsValid(); }
};