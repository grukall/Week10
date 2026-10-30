#pragma once

#include <functional>
#include "Core/Types.h"
#include "Containers/Array.h"
struct FDelegateHandle
{
	uint64 Id = 0;
	bool IsValid() const { return Id != 0; } 
};

template<typename... Args>
class TMulticastDelegate
{
public:
	TMulticastDelegate() = default;
	TMulticastDelegate (const TMulticastDelegate&) = delete;
	TMulticastDelegate& operator=(const TMulticastDelegate&) = delete;

	using HandlerType = std::function<void(Args...)>;
	FDelegateHandle Add(const HandlerType& Handler, const void* Owner);
	template<typename T>
	FDelegateHandle AddDynamic(T* Instance, void (T::* Func)(Args...));
	void Broadcast(Args... args);
	void Remove(const FDelegateHandle& InHandle);
	void RemoveAll(const void* InOwner);
	void Clear();
	bool IsBound() const;

private:
	struct FDelegateInstance
	{
		// Remove(Handle)가 찾는 기준
		FDelegateHandle Handle;          
		// RemoveAll(Owner)가 찾는 기준. 비교만 하고 역참조하지 않는다
		const void* Owner = nullptr;

		// Broadcast가 실제로 부르는 함수
		HandlerType Handler;            
		// Broadcast 중 해제되면 표시만 하고 끝난 뒤 Compact에서 뺀다
		bool bPendingRemove = false;     
	};

	void Compact();

private:
	// 등록한 함수들이 여기에 저장된다
	TArray<FDelegateInstance> InvocationList;
	// 핸들 번호 발급용
	uint64 NextId = 0;          
	// 지금 실행 중인 이 Delegate의 Broadcast 중첩 수.
	// 0이 아니면 순회 중이므로 Remove·RemoveAll은 bPendingRemove 표시만 하고, 실제 삭제는 가장 바깥 Broadcast가 끝날 때 Compact에서 한다.
	// 핸들러가 같은 이벤트를 다시 일으키면(예: OnScoreChanged 핸들러가 AddScore 호출) Broadcast가 겹치므로 bool이 아니라 수로 센다.
	int32 BroadcastDepth = 0;
	
};

// Add: 완성된 함수(람다, 일반 함수)를 그대로 넘기고 핸들을 반환한다.
template<typename... Args>
FDelegateHandle TMulticastDelegate<Args...>::Add(const HandlerType& Handler, const void* Owner)
{
	FDelegateInstance Instance;
	Instance.Handle.Id = ++NextId;
	Instance.Handler = Handler;
	Instance.Owner = Owner;
	InvocationList.Add(Instance);

	return Instance.Handle;
}

// AddDynamic: 객체와 멤버 함수를 따로 넘김. 묶는 건 Delegate가 해 줌
//언리얼의 AddDynamic은 함수 이름으로 저장하는 Dynamic Delegate 전용이고, 
//우리가 만든 건 멤버 함수 포인터를 람다로 감싸는 방식이라 언리얼 기준으로는 AddRaw(보호 없음)나 AddUObject(약한 참조)에 해당
template<typename... Args>
template<typename T>
FDelegateHandle TMulticastDelegate<Args...>::AddDynamic(T* Instance, void (T::* Func)(Args...))
{
	// 멤버 함수는 객체가 있어야 불리므로 둘을 람다로 묶고, 객체를 Owner로 기록한다
	return Add([Instance, Func](Args... InArgs)
		{
			(Instance->*Func)(InArgs...);
		}, Instance);
}



template<typename ...Args>
inline void TMulticastDelegate<Args...>::Broadcast(Args ...args)
{
	++BroadcastDepth;

	// 시작할 때의 개수만 돈다. 순회 중에 추가된 등록은 다음 Broadcast부터 불린다.
	const int32 Count = InvocationList.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// 앞의 핸들러가 뒤의 등록을 해제했을 수 있으니 매번 확인한다.
		if (InvocationList[Index].bPendingRemove)
			continue;

		HandlerType HandlerCopy = InvocationList[Index].Handler;
		HandlerCopy(args...);
	}

	if (--BroadcastDepth == 0)
		Compact();
}

template<typename ...Args>
inline void TMulticastDelegate<Args...>::Remove(const FDelegateHandle& InHandle)
{
	for (FDelegateInstance& Instance : InvocationList)
	{
		if (Instance.Handle.Id == InHandle.Id)
		{
			Instance.bPendingRemove = true;
			break;
		}
	}

	if (BroadcastDepth == 0)
		Compact();
}

template<typename ...Args>
inline void TMulticastDelegate<Args...>::RemoveAll(const void* InOwner)
{
	// Owner 없이 등록된 항목들이 한꺼번에 지워지지 않도록 막는다.
	if (!InOwner)
		return;

	for (FDelegateInstance& Instance : InvocationList)
	{
		if (Instance.Owner == InOwner)
			Instance.bPendingRemove = true;
	}

	// Broadcast 중이면 표시만 해 두고, 가장 바깥 Broadcast가 끝날 때 Compact한다.
	if (BroadcastDepth == 0)
		Compact();
}

// Clear: Owner·Handle과 상관없이 전부 해제한다. 재시작처럼 Delegate를 가진 쪽이 구독을 모두 끊을 때 쓴다.
template<typename ...Args>
inline void TMulticastDelegate<Args...>::Clear()
{
	// 핸들러 안에서 불릴 수 있으므로 바로 비우지 않고 다른 해제와 같은 방식을 따른다.
	for (FDelegateInstance& Instance : InvocationList)
		Instance.bPendingRemove = true;

	if (BroadcastDepth == 0)
		Compact();
}

// IsBound: 아직 해제되지 않은 등록이 하나라도 있는지. 표시만 된 항목은 이미 해제된 것으로 본다.
template<typename ...Args>
inline bool TMulticastDelegate<Args...>::IsBound() const
{
	for (const FDelegateInstance& Instance : InvocationList)
	{
		if (!Instance.bPendingRemove)
			return true;
	}
	return false;
}

// bPendingRemove로 표시된 등록을 실제로 뺀다.
// 뒤에서부터 지워야 앞의 인덱스가 밀리지 않고, RemoveAt으로 등록 순서를 유지한다.
template<typename... Args>
void TMulticastDelegate<Args...>::Compact()
{
	for (int32 Index = InvocationList.Num() - 1; Index >= 0; --Index)
	{
		if (InvocationList[Index].bPendingRemove)
			InvocationList.RemoveAt(Index);
	}
}
