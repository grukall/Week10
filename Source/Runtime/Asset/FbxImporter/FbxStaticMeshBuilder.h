#pragma once

struct FStaticMeshImportData;
struct FStaticMeshData;

// FBX 중간 데이터를 기존 스태틱 메시의 CPU 데이터로 변환한다.
class FFbxStaticMeshBuilder
{
public:
    static bool Build(const FStaticMeshImportData& Source, FStaticMeshData& OutData);
};
