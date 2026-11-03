#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include <iosfwd>

struct FStaticMeshData;

// Cook이 끝난 FStaticMeshData를 .bin으로 저장하고 읽는다.
class FStaticMeshBake
{
public:
	static TUniquePtr<FStaticMeshData> ReadBaked(const FString& BinPath);
	static void WriteBaked(const FString& BinPath, const FStaticMeshData& Data);

	// FBX 캐시 안에서도 기존 스태틱 바이너리 형식을 그대로 사용한다.
	static TUniquePtr<FStaticMeshData> ReadBaked(std::istream& In);
	static bool WriteBaked(std::ostream& Out, const FStaticMeshData& Data);
};
