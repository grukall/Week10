#pragma once

#include "Containers/Array.h"
#include "UObjectHash.h"
#include "UObject/Object.h" 	// IsPendingKill

#undef GetObject

template <typename T>
class TObjectIterator
{
public:
	TObjectIterator(bool bIncludeDerivedClasses = true)
		:Index(-1)
	{
		GetObjectsOfClass(T::StaticClass(), ObjectArray, bIncludeDerivedClasses);
		Advance();
	}

	inline explicit operator bool() const 
	{
		return ObjectArray.IsValidIndex(Index);
	}

	bool operator!() const
	{
		return !(bool)*this;
	}

	inline T* operator*() const 
	{
		return (T*)GetObject();
	};

	inline T* operator->() const
	{
		return (T*)GetObject();
	}
	inline void operator++() { Advance(); }
	void operator++(int _dummy)
	{
		Advance();
	}

	bool operator==(const TObjectIterator& Rhs) const { return Index == Rhs.Index; }
	bool operator!=(const TObjectIterator& Rhs) const { return Index != Rhs.Index; }

protected:
	UObject* GetObject() const
	{
		return ObjectArray[Index];
	}

	inline bool Advance()
	{
		while (++Index < ObjectArray.Num())
		{
			// Destroy 예정 객체는 순회하지 않는다
			UObject* Object = GetObject();
			if (Object && !Object->IsPendingKill())
			{
				return true;
			}
		}
		return false;
	}
	
protected:
	TArray<UObject*> ObjectArray;
	int32 Index;
};
