#pragma once

struct FSkeletalMeshImportData;
struct FSkeletalMeshData;

// FBX SDK와 무관하게, 그룹 하나의 중간 데이터를 렌더링용 CPU 데이터로 변환한다.
class FSkeletalMeshBuilder
{
public:
    static bool Build(const FSkeletalMeshImportData& Source, FSkeletalMeshData& OutData);
};
