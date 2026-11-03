#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Render/StaticMeshData.h"
#include "Render/SkeletalMeshData.h"

// GPU 자원이 만들어지기 전의 결과. 한 FBX는 static 하나 또는 skeletal 그룹 배열이다.
struct FFbxMeshData
{
    TUniquePtr<FStaticMeshData> StaticMesh;

    // 배열 순서가 기존 #GroupN 에셋 키와 대응한다.
    TArray<FSkeletalMeshData> SkeletalGroups;
};

class FFbxMeshBake
{
public:
    static bool ReadBaked(const FString& FbxPath, FFbxMeshData& OutData);
    static bool WriteBaked(const FString& FbxPath, const FFbxMeshData& Data);
};
