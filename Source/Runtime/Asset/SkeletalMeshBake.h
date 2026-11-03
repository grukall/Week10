#pragma once

#include <iosfwd>

struct FSkeletalMeshData;

// 원본 파일 형식과 무관하게 스켈레탈 CPU 데이터를 바이너리 스트림으로 읽고 쓴다.
class FSkeletalMeshBake
{
public:
    static bool ReadBaked(std::istream& In, FSkeletalMeshData& OutData);
    static bool WriteBaked(std::ostream& Out, const FSkeletalMeshData& Data);
};
