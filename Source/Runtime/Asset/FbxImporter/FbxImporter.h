#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"

struct FSkeletalMeshImportData;
struct FFbxImportData;

class FFbxImporter
{
public:
    // 파일 로드 → 좌표계 통일 → 그룹별 데이터 추출 → 엔진 자료형으로 결과 전달.
    static bool ParseFbx(const FString& Path, TArray<FSkeletalMeshImportData>& OutData);
    static bool ParseFbx(const FString& Path, FFbxImportData& OutData);

private:
    // FBX 파일 위치를 기준으로 텍스처 경로를 정리하고 실제 파일이 있는지 확인한다.
    static bool ResolveTexturePath(const FString& FbxPath, FString& TexturePath);
};