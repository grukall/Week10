#pragma once

#include "Containers/Array.h"
#include "Math/Matrix.h"
#include "Core/EngineString.h"

struct FSkeletalMeshData;

namespace FbxPoseExporter
{
    bool ExportReboundFbx(const FString& SourcePath, const FString& DestinationPath, FSkeletalMeshData& MeshData, TArray<FMatrix>& LocalPoseMatrices);
}
