#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"

class UMaterial;

class USkeletalMesh : public URenderAsset
{
    DECLARE_CLASS(USkeletalMesh, URenderAsset)

public:
    FSkeletalMeshData MeshData;

    // MaterialSlots와 같은 인덱스의 런타임 재질. 같은 메시의 인스턴스들이 공유한다.
    TArray<UMaterial*> Materials;
    // 토폴로지는 에셋이 공유하고, 변형되는 정점 버퍼는 컴포넌트가 소유한다.
    TUniquePtr<FIndexBuffer> IndexBuffer;

    const FSkeletalMeshData& GetMeshData() const { return MeshData; }
    int32 GetNumMaterials() const { return static_cast<int32>(Materials.Num()); }
    UMaterial* GetMaterial(uint32 SlotIndex) const;
    FIndexBuffer* GetIndexBuffer() const { return IndexBuffer.get(); }
};
