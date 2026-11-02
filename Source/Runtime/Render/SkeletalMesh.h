#pragma once

#include "Asset/RenderAsset.h"
#include "Render/SkeletalMeshData.h"

class USkeletalMesh : public URenderAsset
{
    DECLARE_CLASS(USkeletalMesh, URenderAsset)
             
public:
    FSkeletalMeshData MeshData;

    const FSkeletalMeshData& GetMeshData() const { return MeshData; }
};