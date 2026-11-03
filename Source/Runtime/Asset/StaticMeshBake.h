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

	// 다른 캐시 파일에서도 스태틱 바이너리 형식을 재사용한다.
	static TUniquePtr<FStaticMeshData> ReadBaked(std::istream& In);
	static bool WriteBaked(std::ostream& Out, const FStaticMeshData& Data);

	// 캐시에서 복원한 데이터의 인덱스 범위와 수치를 검사한다.
	static bool Validate(const FStaticMeshData& Data);
};
