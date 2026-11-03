#include "EnginePCH.h"
#include "SkeletalMeshBake.h"

#include "Render/SkeletalMeshData.h"
#include <istream>
#include <ostream>
#include <type_traits>

namespace
{
    constexpr uint32 MaxArrayElements = 16000000;
    constexpr uint32 MaxStringLength = 4096;
    constexpr uint32 MaxBoneCount = 65536;
    constexpr uint32 MaxMaterialCount = 65536;

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

    template<typename T>
    void WriteArray(std::ostream& Out, const TArray<T>& Values)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        WritePod(Out, static_cast<uint32>(Values.Num()));
        Out.write(reinterpret_cast<const char*>(Values.GetData()), sizeof(T) * Values.size());
    }

    template<typename T>
    bool ReadArray(std::istream& In, TArray<T>& Values)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        uint32 Count = 0;
        if (!ReadPod(In, Count) || Count > MaxArrayElements)
            return false;

        // 잘못된 개수 때문에 파일 크기보다 큰 배열을 할당하지 않도록 한다.
        const std::streampos DataStart = In.tellg();
        In.seekg(0, std::ios::end);
        const std::streampos FileEnd = In.tellg();
        In.seekg(DataStart);

        const std::streamoff RequiredBytes = static_cast<std::streamoff>(sizeof(T) * static_cast<uint64>(Count));
        if (!In || FileEnd - DataStart < RequiredBytes)
            return false;

        Values.SetNum(static_cast<int32>(Count));
        return static_cast<bool>(In.read(reinterpret_cast<char*>(Values.GetData()), sizeof(T) * Count));
    }

    void WriteString(std::ostream& Out, const FString& Value)
    {
        WritePod(Out, static_cast<uint32>(Value.size()));
        Out.write(Value.data(), Value.size());
    }

    bool ReadString(std::istream& In, FString& Value)
    {
        uint32 Count = 0;
        if (!ReadPod(In, Count) || Count > MaxStringLength)
            return false;
        Value.resize(Count);
        return static_cast<bool>(In.read(Value.data(), Count));
    }

    // FMatrix는 단순 바이트 복사 타입이 아니므로 성분별로 저장한다.
    void WriteMatrix(std::ostream& Out, const FMatrix& Matrix)
    {
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Column = 0; Column < 4; ++Column)
                WritePod(Out, Matrix.M[Row][Column]);
        }
    }

    bool ReadMatrix(std::istream& In, FMatrix& Matrix)
    {
        for (int32 Row = 0; Row < 4; ++Row)
        {
            for (int32 Column = 0; Column < 4; ++Column)
            {
                if (!ReadPod(In, Matrix.M[Row][Column]))
                    return false;
            }
        }
        return true;
    }
}

bool FSkeletalMeshBake::ReadBaked(std::istream& In, FSkeletalMeshData& OutData)
{
    if (!ReadArray(In, OutData.Vertices) ||
        !ReadArray(In, OutData.Indices) ||
        !ReadArray(In, OutData.Influences) ||
        !ReadArray(In, OutData.Sections) ||
        !ReadPod(In, OutData.ReferenceBounds))
        return false;

    uint32 BoneCount = 0;
    if (!ReadPod(In, BoneCount) || BoneCount > MaxBoneCount)
        return false;

    for (uint32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        FSkeletalMeshRefBone Bone;
        FString BoneName;
        if (!ReadString(In, BoneName) ||
            !ReadPod(In, Bone.ParentIndex) ||
            !ReadPod(In, Bone.SubtreeEndIndex) ||
            !ReadMatrix(In, Bone.RefLocalMatrix) ||
            !ReadMatrix(In, Bone.InverseBindMatrix))
            return false;

        Bone.Name = FName(BoneName);
        OutData.RefBones.Add(std::move(Bone));
    }

    uint32 MaterialCount = 0;
    if (!ReadPod(In, MaterialCount) || MaterialCount > MaxMaterialCount)
        return false;

    for (uint32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        FSkeletalMaterialSlot Material;
        if (!ReadString(In, Material.Name) ||
            !ReadPod(In, Material.BaseColor) ||
            !ReadPod(In, Material.SpecularColor) ||
            !ReadPod(In, Material.Shininess) ||
            !ReadString(In, Material.DiffuseTexturePath) ||
            !ReadString(In, Material.NormalTexturePath))
            return false;

        OutData.MaterialSlots.Add(std::move(Material));
    }
    return static_cast<bool>(In);
}

bool FSkeletalMeshBake::WriteBaked(std::ostream& Out, const FSkeletalMeshData& Data)
{
    WriteArray(Out, Data.Vertices);
    WriteArray(Out, Data.Indices);
    WriteArray(Out, Data.Influences);
    WriteArray(Out, Data.Sections);
    WritePod(Out, Data.ReferenceBounds);

    // FName의 실행 중 인덱스 대신 이름을 저장하고 로드할 때 다시 등록한다.
    WritePod(Out, static_cast<uint32>(Data.RefBones.Num()));
    for (const FSkeletalMeshRefBone& Bone : Data.RefBones)
    {
        WriteString(Out, Bone.Name.ToString());
        WritePod(Out, Bone.ParentIndex);
        WritePod(Out, Bone.SubtreeEndIndex);
        WriteMatrix(Out, Bone.RefLocalMatrix);
        WriteMatrix(Out, Bone.InverseBindMatrix);
    }

    WritePod(Out, static_cast<uint32>(Data.MaterialSlots.Num()));
    for (const FSkeletalMaterialSlot& Material : Data.MaterialSlots)
    {
        WriteString(Out, Material.Name);
        WritePod(Out, Material.BaseColor);
        WritePod(Out, Material.SpecularColor);
        WritePod(Out, Material.Shininess);
        WriteString(Out, Material.DiffuseTexturePath);
        WriteString(Out, Material.NormalTexturePath);
    }
    return static_cast<bool>(Out);
}
