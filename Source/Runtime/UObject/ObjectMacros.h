#pragma once

#include "Core/Types.h"

enum class EObjectFlags : uint32
{
	RF_NoFlags = 0x00000000, // 플래그 없음
	RF_Transactional = 0x00000008, // Undo Redo 관리 대상
	RF_Transient = 0x00000040, // 직렬화 제외 플래그
	RF_DefaultSubObject = 0x00040000,
	RF_PendingKill = 0x20000000, // Destroy 요청됨. 메모리는 Frame 끝에서 지움.
	RF_AllFlags = 0xffffffff, // 모든 플래그
};

DEFINE_ENUM_OPERATORS(EObjectFlags)