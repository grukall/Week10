#include "EnginePCH.h"
#include "SkeletalMeshBuilder.h"

#include "FbxImportData.h"
#include "Render/SkeletalMeshData.h"
#include "Core/EngineLog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
    bool IsFinite(const FVector& V)
    {
        return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
    }

    bool IsFinite(const FVector2& V)
    {
        return std::isfinite(V.X) && std::isfinite(V.Y);
    }

    bool IsFinite(const FVector4& V)
    {
        return std::isfinite(V.X) && std::isfinite(V.Y) &&
            std::isfinite(V.Z) && std::isfinite(V.W);
    }

    bool IsFinite(const FMatrix& M)
    {
        for (int32 Row = 0; Row < 4; ++Row)
            for (int32 Column = 0; Column < 4; ++Column)
                if (!std::isfinite(M.M[Row][Column]))
                    return false;
        return true;
    }

    bool InvertMatrix(const FMatrix& Matrix, FMatrix& OutInverse)
    {
        const float Determinant = Matrix.Determinant();
        if (!std::isfinite(Determinant) || std::fabs(Determinant) <= std::numeric_limits<float>::min())
            return false;

        OutInverse = Matrix.Inverse();
        return IsFinite(OutInverse);
    }

    FVector FallbackTangent(const FVector& Normal)
    {
        const FVector Axis = std::fabs(Normal.Z) < 0.9f
            ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
        return FVector::Cross(Axis, Normal).GetSafeNormal();
    }

    // 데이터의 모든 정점은 같은 기준 공간에 있으므로 본의 글로벌 역행렬을 저장한다.
    bool BuildBones(const FSkeletalMeshImportData& Source, FSkeletalMeshData& Built)
    {
        const auto& Bones = Source.Skeleton.RefBones;
        for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
        {
            const FImportedRefBone& Imported = Bones[BoneIndex];
            if (Imported.ParentIndex < -1 || Imported.ParentIndex >= BoneIndex ||
                !IsFinite(Imported.RefGlobalMatrix))
                return false;

            FSkeletalMeshRefBone Bone;
            Bone.Name = FName(Imported.Name);
            Bone.ParentIndex = Imported.ParentIndex;
            Bone.SubtreeEndIndex = BoneIndex + 1;

            if (!InvertMatrix(Imported.RefGlobalMatrix, Bone.InverseBindMatrix))
                return false;

            if (Imported.ParentIndex >= 0)
            {
                FMatrix ParentInverse;
                if (!InvertMatrix(Bones[Imported.ParentIndex].RefGlobalMatrix, ParentInverse))
                    return false;

                // 행벡터 규칙: 본 로컬 * 부모 글로벌 = 본 글로벌.
                Bone.RefLocalMatrix = Imported.RefGlobalMatrix * ParentInverse;
            }
            else
            {
                Bone.RefLocalMatrix = Imported.RefGlobalMatrix;
            }

            if (!IsFinite(Bone.RefLocalMatrix))
                return false;
            Built.RefBones.Add(std::move(Bone));
        }

        const int32 BoneCount = static_cast<int32>(Built.RefBones.Num());

        for (int32 BoneIndex = BoneCount - 1; BoneIndex >= 0; --BoneIndex)
        {
            const FSkeletalMeshRefBone& Bone = Built.RefBones[BoneIndex];

            if (Bone.ParentIndex >= 0)
            {
                FSkeletalMeshRefBone& Parent = Built.RefBones[Bone.ParentIndex];
                Parent.SubtreeEndIndex = std::max(Parent.SubtreeEndIndex,Bone.SubtreeEndIndex);
            }
        }

        return true;
    }

    // 가중치는 Control Point별로 정리한 뒤, 해당 Point를 쓰는 모든 Wedge 정점에 복사한다.
    bool BuildPointInfluences(const FImportedMesh& Mesh, int32 BoneCount,
        TArray<TArray<FSkeletalMeshInfluence>>& PointInfluences)
    {
        PointInfluences.SetNum(Mesh.Points.Num());

        if (Mesh.AttachedBoneIndex >= 0)
        {
            if (Mesh.AttachedBoneIndex >= BoneCount || !Mesh.Influences.IsEmpty())
                return false;

            for (auto& Influences : PointInfluences)
                Influences.Add({ Mesh.AttachedBoneIndex, 1.0f });
            return true;
        }

        if (Mesh.SkinningType == EImportedSkinningType::None || BoneCount == 0)
            return false;

        for (const FImportedInfluence& Influence : Mesh.Influences)
        {
            if (Influence.ControlPointIndex < 0 || Influence.ControlPointIndex >= Mesh.Points.Num() ||
                Influence.BoneIndex < 0 || Influence.BoneIndex >= BoneCount ||
                !std::isfinite(Influence.Weight) || Influence.Weight < 0.0f)
                return false;

            if (Influence.Weight == 0.0f)
                continue;

            auto& PerPoint = PointInfluences[Influence.ControlPointIndex];
            bool bMerged = false;
            for (auto& Existing : PerPoint)
            {
                if (Existing.BoneIndex == Influence.BoneIndex)
                {
                    Existing.Weight += Influence.Weight;
                    bMerged = true;
                    break;
                }
            }
            if (!bMerged)
                PerPoint.Add({ Influence.BoneIndex, Influence.Weight });
        }

        for (auto& PerPoint : PointInfluences)
        {
            double Sum = 0.0;
            for (const auto& Influence : PerPoint)
                Sum += Influence.Weight;

            if (!std::isfinite(Sum))
                return false;
            if (Sum == 0.0)
                continue; // 사용하지 않는 Point에는 가중치가 없어도 된다.

            for (auto& Influence : PerPoint)
                Influence.Weight = static_cast<float>(static_cast<double>(Influence.Weight) / Sum);
        }
        return true;
    }

    bool AppendMesh(const FImportedMesh& Mesh, FSkeletalMeshData& Built,
        TArray<TArray<uint32>>& MaterialIndices, int32& DefaultMaterialIndex,
        int32& DegenerateTriangleCount)
    {
        if (Mesh.SkinningType == EImportedSkinningType::DualQuaternion ||
            Mesh.SkinningType == EImportedSkinningType::Blend)
        {
            HTR_LOG(Error, "[Skeletal Build] Runtime skinning type unsupported: Mesh={}", Mesh.Name);
            return false; // 지금 FSkeletalMeshData에는 혼합 방식과 DQ 데이터가 없다.
        }

        FMatrix InverseMeshBind;
        if (!InvertMatrix(Mesh.MeshToSkeletonBindMatrix, InverseMeshBind))
            return false;

        const FMatrix NormalMatrix = InverseMeshBind.GetTransposed();
        const bool bFlipWinding = Mesh.MeshToSkeletonBindMatrix.Determinant() < 0.0f;

        TArray<TArray<FSkeletalMeshInfluence>> PointInfluences;
        if (!BuildPointInfluences(Mesh, Built.RefBones.Num(), PointInfluences))
            return false;

        const uint32 VertexBase = static_cast<uint32>(Built.Vertices.Num());

        // Wedge 하나를 최종 정점 하나로 만든다. 이후 중복 정점 합치기는 별도 최적화다.
        for (const FImportedWedge& Wedge : Mesh.Wedges)
        {
            if (Wedge.ControlPointIndex < 0 || Wedge.ControlPointIndex >= Mesh.Points.Num())
                return false;

            const auto& Weights = PointInfluences[Wedge.ControlPointIndex];
            if (Weights.IsEmpty())
            {
                HTR_LOG(Error, "[Skeletal Build] No weight on used point: Mesh={}, Point={}", Mesh.Name, Wedge.ControlPointIndex);
                return false;
            }

            FSkeletalMeshVertex Vertex;
            Vertex.Vertex.Position = Mesh.MeshToSkeletonBindMatrix.TransformPosition(Mesh.Points[Wedge.ControlPointIndex]);
            if (!IsFinite(Vertex.Vertex.Position))
                return false;

            Vertex.Vertex.UV = Wedge.UVs.IsEmpty() ? FVector2(0.0f, 0.0f) : Wedge.UVs[0];
			Vertex.Vertex.UV.Y = 1.0f - Vertex.Vertex.UV.Y; // FBX는 V축이 반대
            Vertex.Vertex.Color = FVector4(Wedge.Color.R, Wedge.Color.G, Wedge.Color.B, Wedge.Color.A);
            if (!IsFinite(Vertex.Vertex.UV) || !IsFinite(Vertex.Vertex.Color))
                return false;

            if (Mesh.bHasNormals)
                Vertex.Vertex.Normal = NormalMatrix.TransformVector(Wedge.Normal).GetSafeNormal();

            Vertex.InfluenceInfo.FirstInfluence = static_cast<uint32>(Built.Influences.Num());
            Vertex.InfluenceInfo.InfluenceCount = static_cast<uint32>(Weights.Num());
            for (const FSkeletalMeshInfluence& Weight : Weights)
                Built.Influences.Add(Weight);

            Built.Vertices.Add(std::move(Vertex));
        }

        for (const FImportedPolygon& Polygon : Mesh.Polygons)
        {
            if (Polygon.WedgeIndices.Num() != 3)
                return false; // FBX SDK의 삼각형화가 끝난 데이터만 받는다.

            uint32 V[3];
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 WedgeIndex = Polygon.WedgeIndices[Corner];
                if (WedgeIndex < 0 || WedgeIndex >= Mesh.Wedges.Num())
                    return false;
                V[Corner] = VertexBase + static_cast<uint32>(WedgeIndex);
            }
            if (bFlipWinding)
                std::swap(V[1], V[2]);

            const FVector& P0 = Built.Vertices[V[0]].Vertex.Position;
            const FVector& P1 = Built.Vertices[V[1]].Vertex.Position;
            const FVector& P2 = Built.Vertices[V[2]].Vertex.Position;
            const FVector Edge1 = P1 - P0;
            const FVector Edge2 = P2 - P0;

            // 작은 삼각형도 실제 면적이 0이 아니면 남긴다. GetSafeNormal()의
            // 기본 허용 오차로 먼저 정규화하면 작은 면까지 0으로 처리될 수 있다.
            const double E1X = static_cast<double>(P1.X) - P0.X;
            const double E1Y = static_cast<double>(P1.Y) - P0.Y;
            const double E1Z = static_cast<double>(P1.Z) - P0.Z;
            const double E2X = static_cast<double>(P2.X) - P0.X;
            const double E2Y = static_cast<double>(P2.Y) - P0.Y;
            const double E2Z = static_cast<double>(P2.Z) - P0.Z;
            const double NX = E1Y * E2Z - E1Z * E2Y;
            const double NY = E1Z * E2X - E1X * E2Z;
            const double NZ = E1X * E2Y - E1Y * E2X;
            const double AreaScale = std::hypot(NX, NY, NZ);

            if (!std::isfinite(AreaScale) || AreaScale == 0.0)
            {
                ++DegenerateTriangleCount;
                continue;
            }

            const FVector FaceNormal(
                static_cast<float>(NX / AreaScale),
                static_cast<float>(NY / AreaScale),
                static_cast<float>(NZ / AreaScale));

            const FVector2& UV0 = Built.Vertices[V[0]].Vertex.UV;
            const FVector2& UV1 = Built.Vertices[V[1]].Vertex.UV;
            const FVector2& UV2 = Built.Vertices[V[2]].Vertex.UV;
            const float DU1 = UV1.X - UV0.X, DV1 = UV1.Y - UV0.Y;
            const float DU2 = UV2.X - UV0.X, DV2 = UV2.Y - UV0.Y;
            const float UvDet = DU1 * DV2 - DV1 * DU2;

            FVector FaceTangent = FallbackTangent(FaceNormal);
            FVector FaceBinormal = FVector::Cross(FaceNormal, FaceTangent);
            if (std::isfinite(UvDet) && std::fabs(UvDet) > 0.00000001f)
            {
                const FVector Candidate = (Edge1 * DV2 - Edge2 * DV1) / UvDet;
                const FVector CandidateBinormal = (Edge2 * DU1 - Edge1 * DU2) / UvDet;
                if (IsFinite(Candidate) && Candidate.SizeSquared() > 0.000000000001f)
                {
                    FaceTangent = Candidate;
                    FaceBinormal = CandidateBinormal;
                }
            }

            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                FSkeletalMeshVertex& Vertex = Built.Vertices[V[Corner]];
                FVector Normal = Vertex.Vertex.Normal;
                if (!Mesh.bHasNormals || !IsFinite(Normal) || Normal.SizeSquared() < 0.000000000001f)
                    Normal = FaceNormal;
                Vertex.Vertex.Normal = Normal;

                FVector Tangent = FaceTangent;
                float Sign = 1.0f;
                if (Mesh.bHasTangents)
                {
                    const int32 WedgeIndex = static_cast<int32>(V[Corner] - VertexBase);
                    const FVector4& Original = Mesh.Wedges[WedgeIndex].Tangent;
                    Tangent = Mesh.MeshToSkeletonBindMatrix.TransformVector(FVector(Original.X, Original.Y, Original.Z));
                    Sign = Original.W * (bFlipWinding ? -1.0f : 1.0f);
                }

                Tangent = (Tangent - Normal * FVector::Dot(Normal, Tangent)).GetSafeNormal();
                if (!IsFinite(Tangent) || Tangent.SizeSquared() < 0.000000000001f)
                    Tangent = FallbackTangent(Normal);

                if (!Mesh.bHasTangents)
                    Sign = FVector::Dot(FVector::Cross(Normal, Tangent), FaceBinormal) < 0.0f ? -1.0f : 1.0f;

                Vertex.Vertex.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, Sign);
                if (!IsFinite(Vertex.Vertex.Normal) || !IsFinite(Vertex.Vertex.Tangent))
                    return false;
            }

            int32 MaterialIndex = Polygon.MaterialSlotIndex;
            if (MaterialIndex == -1)
            {
                if (DefaultMaterialIndex < 0)
                {
                    DefaultMaterialIndex = Built.MaterialSlots.Num();
                    FSkeletalMaterialSlot DefaultSlot;
                    DefaultSlot.Name = "Default";
                    Built.MaterialSlots.Add(std::move(DefaultSlot));
                    MaterialIndices.Add(TArray<uint32>());
                }
                MaterialIndex = DefaultMaterialIndex;
            }
            if (MaterialIndex < 0 || MaterialIndex >= MaterialIndices.Num())
                return false;

            for (uint32 VertexIndex : V)
                MaterialIndices[MaterialIndex].Add(VertexIndex);
        }

        return true;
    }

    bool ValidateBuiltData(const FSkeletalMeshData& Built)
    {
        if (Built.Vertices.IsEmpty() || Built.Indices.IsEmpty() || Built.Indices.Num() % 3 != 0)
            return false;

        uint32 ExpectedStart = 0;
        for (const FSkeletalMeshSection& Section : Built.Sections)
        {
            if (Section.StartIndex != ExpectedStart || Section.IndexCount == 0 || Section.IndexCount % 3 != 0 ||
                Section.MaterialSlotIndex >= static_cast<uint32>(Built.MaterialSlots.Num()))
                return false;
            ExpectedStart += Section.IndexCount;
        }
        if (ExpectedStart != static_cast<uint32>(Built.Indices.Num()))
            return false;

        for (uint32 Index : Built.Indices)
            if (Index >= static_cast<uint32>(Built.Vertices.Num()))
                return false;

        for (const FSkeletalMeshVertex& Vertex : Built.Vertices)
        {
            const auto& Info = Vertex.InfluenceInfo;
            if (Info.InfluenceCount == 0 || Info.FirstInfluence > static_cast<uint32>(Built.Influences.Num()) ||
                Info.InfluenceCount > static_cast<uint32>(Built.Influences.Num()) - Info.FirstInfluence)
                return false;

            float Sum = 0.0f;
            for (uint32 InfluenceIndex = Info.FirstInfluence; InfluenceIndex < Info.FirstInfluence + Info.InfluenceCount; ++InfluenceIndex)
            {
                const auto& Influence = Built.Influences[InfluenceIndex];
                if (Influence.BoneIndex < 0 || Influence.BoneIndex >= Built.RefBones.Num() ||
                    !std::isfinite(Influence.Weight) || Influence.Weight < 0.0f)
                    return false;
                Sum += Influence.Weight;
            }
            if (std::fabs(Sum - 1.0f) > 0.0001f)
                return false;
        }
        return true;
    }
}

bool FSkeletalMeshBuilder::Build(const FSkeletalMeshImportData& Source, FSkeletalMeshData& OutData)
{
    FSkeletalMeshData Built; // 성공했을 때만 호출자의 OutData를 교체한다.

    if (!BuildBones(Source, Built))
    {
        HTR_LOG(Error, "[Skeletal Build] Invalid reference skeleton: Group={}", Source.Name);
        return false;
    }

    for (const FImportedMaterial& Material : Source.Materials)
    {
        FSkeletalMaterialSlot Slot;
        Slot.Name = Material.Name;
        Slot.BaseColor = Material.BaseColor;
        Slot.SpecularColor = Material.SpecularColor;
        Slot.Shininess = Material.Shininess;
        Slot.DiffuseTexturePath = Material.DiffuseTexturePath;
        Slot.NormalTexturePath = Material.NormalTexturePath;
        Built.MaterialSlots.Add(std::move(Slot));
    }

    TArray<TArray<uint32>> MaterialIndices;
    MaterialIndices.SetNum(Built.MaterialSlots.Num());
    int32 DefaultMaterialIndex = -1;
    int32 DegenerateTriangleCount = 0;

    for (const FImportedMesh& Mesh : Source.Meshes)
    {
        if (!AppendMesh(Mesh, Built, MaterialIndices, DefaultMaterialIndex, DegenerateTriangleCount))
        {
            HTR_LOG(Error, "[Skeletal Build] Conversion failed: Group={}, Mesh={}", Source.Name, Mesh.Name);
            return false;
        }
    }

    // 같은 재질의 삼각형을 연속 배치해 DrawIndexed 구간을 만든다.
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialIndices.Num(); ++MaterialIndex)
    {
        const TArray<uint32>& Triangles = MaterialIndices[MaterialIndex];
        if (Triangles.IsEmpty())
            continue;

        FSkeletalMeshSection Section;
        Section.StartIndex = static_cast<uint32>(Built.Indices.Num());
        Section.IndexCount = static_cast<uint32>(Triangles.Num());
        Section.MaterialSlotIndex = static_cast<uint32>(MaterialIndex);
        Built.Sections.Add(Section);
        Built.Indices.Append(Triangles);
    }

    if (!Built.Indices.IsEmpty())
    {
        const FVector& First = Built.Vertices[Built.Indices[0]].Vertex.Position;
        Built.ReferenceBounds = FBox{ First, First };
        for (uint32 Index : Built.Indices)
        {
            const FVector& P = Built.Vertices[Index].Vertex.Position;
            FVector& Min = Built.ReferenceBounds.Min;
            FVector& Max = Built.ReferenceBounds.Max;
            Min.X = std::min(Min.X, P.X); Min.Y = std::min(Min.Y, P.Y); Min.Z = std::min(Min.Z, P.Z);
            Max.X = std::max(Max.X, P.X); Max.Y = std::max(Max.Y, P.Y); Max.Z = std::max(Max.Z, P.Z);
        }
    }

    // B의 IsValid() 구현 여부와 무관하게 이 변환 결과를 검사한다.
    if (!ValidateBuiltData(Built))
    {
        HTR_LOG(Error, "[Skeletal Build] Invalid result: Group={}", Source.Name);
        return false;
    }

    HTR_LOG(Info, "[Skeletal Build] Group={}, Vertices={}, Triangles={}, Bones={}, Sections={}, SkippedDegenerate={}",
        Source.Name, Built.Vertices.Num(), Built.Indices.Num() / 3, Built.RefBones.Num(), Built.Sections.Num(), DegenerateTriangleCount);
    OutData = std::move(Built);
    return true;
}
