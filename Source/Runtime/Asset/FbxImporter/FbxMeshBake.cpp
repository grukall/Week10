#include "EnginePCH.h"
#include "FbxMeshBake.h"
#include "SkeletalMeshBuilder.h"
#include "Asset/StaticMeshBake.h"
#include "Asset/SkeletalMeshBake.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <type_traits>

namespace
{
    // CPU 데이터 형식 또는 FBX 변환 규칙(UV/좌표계 등)이 바뀌면 올린다.
    constexpr uint32 BakeVersion = 1;
    constexpr uint32 MaxGroupCount = 1024;

    enum class EFbxMeshKind : uint32
    {
        Static,
        Skeletal
    };

    template<typename T>
    void WritePod(std::ostream& Out, const T& Value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        Out.write(reinterpret_cast<const char*>(&Value), sizeof(T));
    }

    template<typename T>
    bool ReadPod(std::istream& In, T& Value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return static_cast<bool>(In.read(reinterpret_cast<char*>(&Value), sizeof(T)));
    }

    bool ReadSourceFileInfo(const FString& Path, uint64& OutFileSize, int64& OutWriteTime)
    {
        std::error_code Error;
        OutFileSize = fs::file_size(Path, Error);
        if (Error)
            return false;
        const fs::file_time_type LastWriteTime = fs::last_write_time(Path, Error);
        if (Error)
            return false;
        OutWriteTime = static_cast<int64>(LastWriteTime.time_since_epoch().count());
        return true;
    }

    // 캐시를 모델 폴더와 함께 옮겨도 이전 에셋 경로가 남지 않도록 한다.
    FString MakeRelativeTexturePath(const FString& TexturePath, const fs::path& ModelDirectory)
    {
        if (TexturePath.empty())
            return {};

        const fs::path AbsoluteTexturePath = fs::absolute(TexturePath).lexically_normal();
        const fs::path AbsoluteModelDirectory = fs::absolute(ModelDirectory).lexically_normal();
        const fs::path RelativePath = AbsoluteTexturePath.lexically_relative(AbsoluteModelDirectory);
        return RelativePath.empty() ? TexturePath : RelativePath.generic_string();
    }

    bool RestoreTexturePath(FString& TexturePath, const fs::path& ModelDirectory)
    {
        if (TexturePath.empty())
            return true;

        TexturePath = (ModelDirectory / fs::path(TexturePath)).lexically_normal().generic_string();
        std::error_code Error;
        return fs::is_regular_file(TexturePath, Error);
    }

    bool IsFiniteColor(const FLinearColor& Color)
    {
        return std::isfinite(Color.R) && std::isfinite(Color.G) && std::isfinite(Color.B) && std::isfinite(Color.A);
    }

    template<typename MaterialSlotType>
    bool RestoreMaterialPaths(TArray<MaterialSlotType>& Materials, const fs::path& ModelDirectory)
    {
        for (MaterialSlotType& Material : Materials)
        {
            if (!IsFiniteColor(Material.BaseColor) || !IsFiniteColor(Material.SpecularColor) || !std::isfinite(Material.Shininess))
                return false;

            if (!RestoreTexturePath(Material.DiffuseTexturePath, ModelDirectory) ||
                !RestoreTexturePath(Material.NormalTexturePath, ModelDirectory))
                return false;
        }
        return true;
    }
}

bool FFbxMeshBake::ReadBaked(const FString& FbxPath, FFbxMeshData& OutData)
{
    uint64 SourceFileSize = 0;
    int64 SourceWriteTime = 0;
    if (!ReadSourceFileInfo(FbxPath, SourceFileSize, SourceWriteTime))
        return false;

    std::ifstream In(FbxPath + ".bin", std::ios::binary);
    char Magic[4]{};
    uint32 Version = 0;
    EFbxMeshKind Kind = EFbxMeshKind::Static;
    uint64 CachedFileSize = 0;
    int64 CachedWriteTime = 0;

    // 파일 종류와 버전이 맞고 원본이 바뀌지 않은 캐시만 사용한다.
    if (!In.read(Magic, sizeof(Magic)) || std::memcmp(Magic, "FBXB", 4) != 0)
        return false;
    if (!ReadPod(In, Version) || Version != BakeVersion)
        return false;
    if (!ReadPod(In, CachedFileSize) || !ReadPod(In, CachedWriteTime) ||
        CachedFileSize != SourceFileSize || CachedWriteTime != SourceWriteTime)
        return false;
    if (!ReadPod(In, Kind))
        return false;

    // 읽기가 전부 성공하기 전에는 호출자가 가진 데이터를 바꾸지 않는다.
    FFbxMeshData Result;
    const fs::path ModelDirectory = fs::path(FbxPath).parent_path();
    if (Kind == EFbxMeshKind::Static)
    {
        Result.StaticMesh = FStaticMeshBake::ReadBaked(In);
        if (!Result.StaticMesh || !FStaticMeshBake::Validate(*Result.StaticMesh))
            return false;
        if (!RestoreMaterialPaths(Result.StaticMesh->MaterialSlots, ModelDirectory))
            return false;
    }
    else if (Kind == EFbxMeshKind::Skeletal)
    {
        uint32 GroupCount = 0;
        if (!ReadPod(In, GroupCount) || GroupCount == 0 || GroupCount > MaxGroupCount)
            return false;

        for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
        {
            FSkeletalMeshData GroupData;
            if (!FSkeletalMeshBake::ReadBaked(In, GroupData) ||
                !FSkeletalMeshBuilder::Validate(GroupData) ||
                !RestoreMaterialPaths(GroupData.MaterialSlots, ModelDirectory))
                return false;
            Result.SkeletalGroups.Add(std::move(GroupData));
        }
    }
    else
    {
        return false;
    }

    if (In.peek() != std::char_traits<char>::eof())
        return false;

    OutData = std::move(Result);
    HTR_LOG(Info, "[FBX Bake] Loaded: {}", FbxPath + ".bin");
    return true;
}

bool FFbxMeshBake::WriteBaked(const FString& FbxPath, const FFbxMeshData& Data)
{
    const bool bStatic = Data.StaticMesh != nullptr;
    if (bStatic)
    {
        if (!Data.SkeletalGroups.IsEmpty() || !FStaticMeshBake::Validate(*Data.StaticMesh))
            return false;
    }
    else if (Data.SkeletalGroups.IsEmpty())
    {
        return false;
    }

    for (const FSkeletalMeshData& Group : Data.SkeletalGroups)
    {
        if (!FSkeletalMeshBuilder::Validate(Group))
            return false;
    }

    uint64 SourceFileSize = 0;
    int64 SourceWriteTime = 0;
    if (!ReadSourceFileInfo(FbxPath, SourceFileSize, SourceWriteTime))
        return false;

    const fs::path BinPath = FbxPath + ".bin";
    const fs::path TemporaryPath = FbxPath + ".bin.tmp";
    std::ofstream Out(TemporaryPath, std::ios::binary | std::ios::trunc);
    if (!Out)
    {
        HTR_LOG(Warning, "[FBX Bake] Could not open for writing: {}", TemporaryPath.generic_string());
        return false;
    }

    Out.write("FBXB", 4);
    WritePod(Out, BakeVersion);
    WritePod(Out, SourceFileSize);
    WritePod(Out, SourceWriteTime);
    const EFbxMeshKind Kind = bStatic ? EFbxMeshKind::Static : EFbxMeshKind::Skeletal;
    WritePod(Out, Kind);

    const fs::path ModelDirectory = fs::path(FbxPath).parent_path();
    if (bStatic)
    {
        FStaticMeshData RelativeMeshData = *Data.StaticMesh;
        for (FStaticMaterialSlot& Material : RelativeMeshData.MaterialSlots)
        {
            Material.DiffuseTexturePath = MakeRelativeTexturePath(Material.DiffuseTexturePath, ModelDirectory);
            Material.NormalTexturePath = MakeRelativeTexturePath(Material.NormalTexturePath, ModelDirectory);
        }
        FStaticMeshBake::WriteBaked(Out, RelativeMeshData);
    }
    else
    {
        WritePod(Out, static_cast<uint32>(Data.SkeletalGroups.Num()));
        for (const FSkeletalMeshData& Group : Data.SkeletalGroups)
        {
            FSkeletalMeshData RelativeGroupData = Group;
            for (FSkeletalMaterialSlot& Material : RelativeGroupData.MaterialSlots)
            {
                Material.DiffuseTexturePath = MakeRelativeTexturePath(Material.DiffuseTexturePath, ModelDirectory);
                Material.NormalTexturePath = MakeRelativeTexturePath(Material.NormalTexturePath, ModelDirectory);
            }
            FSkeletalMeshBake::WriteBaked(Out, RelativeGroupData);
        }
    }

    Out.flush();
    Out.close();
    const bool bFileWritten = static_cast<bool>(Out);

    // 임시 파일을 끝까지 쓴 경우에만 기존 캐시를 교체한다.
    std::error_code Error;
    if (bFileWritten)
        fs::rename(TemporaryPath, BinPath, Error);
    if (!bFileWritten || Error)
    {
        fs::remove(TemporaryPath, Error);
        HTR_LOG(Warning, "[FBX Bake] Could not save: {}", BinPath.generic_string());
        return false;
    }
    HTR_LOG(Info, "[FBX Bake] Saved: {}", BinPath.generic_string());
    return true;
}
