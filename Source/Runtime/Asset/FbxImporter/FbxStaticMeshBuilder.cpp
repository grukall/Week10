#include "EnginePCH.h"
#include "FbxStaticMeshBuilder.h"
#include "FbxImportData.h"
#include "Render/StaticMeshData.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // 같은 원본 점이라도 UV·노멀·색상·접선 방향이 다르면 정점을 나눈다.
    struct FStaticVertexKey
    {
        int32 ControlPointIndex;
        FVector2 UV;
        FVector Normal;
        FVector4 Color;
        float TangentSign;

        bool operator==(const FStaticVertexKey& Other) const
        {
            return ControlPointIndex == Other.ControlPointIndex &&
                UV.X == Other.UV.X && UV.Y == Other.UV.Y &&
                Normal.X == Other.Normal.X && Normal.Y == Other.Normal.Y && Normal.Z == Other.Normal.Z &&
                Color.X == Other.Color.X && Color.Y == Other.Color.Y &&
                Color.Z == Other.Color.Z && Color.W == Other.Color.W &&
                TangentSign == Other.TangentSign;
        }
    };

    struct FStaticVertexKeyHash
    {
        size_t operator()(const FStaticVertexKey& Key) const
        {
            size_t Hash = std::hash<int32>{}(Key.ControlPointIndex);
            const float Values[] = { Key.UV.X, Key.UV.Y, Key.Normal.X, Key.Normal.Y, Key.Normal.Z,
                Key.Color.X, Key.Color.Y, Key.Color.Z, Key.Color.W, Key.TangentSign };

            for (float Value : Values)
                Hash = Hash * 31 + std::hash<float>{}(Value);

            return Hash;
        }
    };

    // 메시 하나의 변환·정점 공유·재질별 인덱스를 빌드 결과에 추가한다.
    bool AppendMesh(const FImportedMesh& Mesh, FStaticMeshData& Built,
        TArray<TArray<uint32>>& MaterialIndices, int32& DefaultMaterialIndex, int32 ControlPointBase)
    {
        const FMatrix& Transform = Mesh.MeshToSkeletonBindMatrix;
        const float Determinant = Transform.Determinant();
        if (!std::isfinite(Determinant) || std::fabs(Determinant) <= std::numeric_limits<float>::min())
            return false;

        const FMatrix NormalTransform = Transform.Inverse().GetTransposed();
        const uint32 VertexBase = static_cast<uint32>(Built.Vertices.Num());
        TArray<FVertexPNCT> WedgeVertices;

        // 위치는 노드 변환, 노멀은 역전치 행렬로 공통 에셋 공간에 옮긴다.
        for (const FImportedWedge& Wedge : Mesh.Wedges)
        {
            if (Wedge.ControlPointIndex < 0 || Wedge.ControlPointIndex >= Mesh.Points.Num())
                return false;

            FVertexPNCT Vertex;
            Vertex.Position = Transform.TransformPosition(Mesh.Points[Wedge.ControlPointIndex]);
            Vertex.UV = Wedge.UVs.IsEmpty() ? FVector2(0, 0) : Wedge.UVs[0];
            Vertex.UV.Y = 1.0f - Vertex.UV.Y;
            Vertex.Color = FVector4(Wedge.Color.R, Wedge.Color.G, Wedge.Color.B, Wedge.Color.A);
            if (Mesh.bHasNormals)
                Vertex.Normal = NormalTransform.TransformVector(Wedge.Normal).GetSafeNormal();
            WedgeVertices.Add(Vertex);
        }

        TArray<uint32> MeshIndices;
        TArray<int32> TriangleMaterialIndices;
        for (const FImportedPolygon& Polygon : Mesh.Polygons)
        {
            if (Polygon.WedgeIndices.Num() != 3)
                return false;

            uint32 Triangle[3];
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 Index = Polygon.WedgeIndices[Corner];
                if (Index < 0 || Index >= WedgeVertices.Num())
                    return false;
                Triangle[Corner] = static_cast<uint32>(Index);
            }
            if (Determinant < 0)
                std::swap(Triangle[1], Triangle[2]);
            const FVector& P0 = WedgeVertices[Triangle[0]].Position;
            const FVector& P1 = WedgeVertices[Triangle[1]].Position;
            const FVector& P2 = WedgeVertices[Triangle[2]].Position;
            const double Edge1X = static_cast<double>(P1.X) - P0.X;
            const double Edge1Y = static_cast<double>(P1.Y) - P0.Y;
            const double Edge1Z = static_cast<double>(P1.Z) - P0.Z;
            const double Edge2X = static_cast<double>(P2.X) - P0.X;
            const double Edge2Y = static_cast<double>(P2.Y) - P0.Y;
            const double Edge2Z = static_cast<double>(P2.Z) - P0.Z;
            const double CrossX = Edge1Y * Edge2Z - Edge1Z * Edge2Y;
            const double CrossY = Edge1Z * Edge2X - Edge1X * Edge2Z;
            const double CrossZ = Edge1X * Edge2Y - Edge1Y * Edge2X;
            const double AreaScale = std::hypot(CrossX, CrossY, CrossZ);
            if (!std::isfinite(AreaScale))
                return false;
            if (AreaScale == 0)
                continue;

            int32 MaterialIndex = Polygon.MaterialSlotIndex;
            if (MaterialIndex == -1)
            {
                if (DefaultMaterialIndex == -1)
                {
                    DefaultMaterialIndex = Built.MaterialSlots.Num();
                    FStaticMaterialSlot Slot;
                    Slot.Name = "Default";
                    Built.MaterialSlots.Add(std::move(Slot));
                    MaterialIndices.Add(TArray<uint32>());
                }
                MaterialIndex = DefaultMaterialIndex;
            }
            if (MaterialIndex < 0 || MaterialIndex >= MaterialIndices.Num())
                return false;
            for (uint32 Index : Triangle)
                MeshIndices.Add(Index);
            TriangleMaterialIndices.Add(MaterialIndex);
        }
        if (!MeshIndices.IsEmpty())
        {
            if (!Mesh.bHasNormals)
                Built.CalculateNormals(WedgeVertices, MeshIndices);
            // 먼저 부호를 계산해 미러 UV 경계가 정점 공유 과정에서 합쳐지지 않도록 한다.
            Built.CalculateTangents(WedgeVertices, MeshIndices);
        }
        for (const FVertexPNCT& Vertex : WedgeVertices)
        {
            const float Values[] = { Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z,
                Vertex.Normal.X, Vertex.Normal.Y, Vertex.Normal.Z, Vertex.UV.X, Vertex.UV.Y,
                Vertex.Color.X, Vertex.Color.Y, Vertex.Color.Z, Vertex.Color.W,
                Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z, Vertex.Tangent.W };
            for (float Value : Values)
                if (!std::isfinite(Value))
                    return false;
        }

        // OBJ와 같이 속성이 같은 꼭짓점을 공유해야 기존 LOD 생성기가 연결된 면을 접을 수 있다.
        TMap<FStaticVertexKey, uint32, FStaticVertexKeyHash> VertexCache;
        TArray<FVertexPNCT> SharedVertices;
        for (int32 Corner = 0; Corner < MeshIndices.Num(); ++Corner)
        {
            const uint32 WedgeIndex = MeshIndices[Corner];
            const FImportedWedge& Wedge = Mesh.Wedges[WedgeIndex];
            const FVertexPNCT& Vertex = WedgeVertices[WedgeIndex];
            FStaticVertexKey Key;
            Key.ControlPointIndex = Wedge.ControlPointIndex;
            Key.UV = Vertex.UV;
            Key.Normal = Vertex.Normal;
            Key.Color = Vertex.Color;
            Key.TangentSign = Vertex.Tangent.W;

            uint32 SharedIndex;
            if (const uint32* Found = VertexCache.Find(Key))
            {
                SharedIndex = *Found;
            }
            else
            {
                SharedIndex = static_cast<uint32>(SharedVertices.Num());
                VertexCache[Key] = SharedIndex;
                SharedVertices.Add(Vertex);

                const int32 SourceIndex = static_cast<int32>(VertexBase + SharedIndex);
                FLODSourceVertex SourceVertex;
                SourceVertex.PositionIndex = ControlPointBase + Wedge.ControlPointIndex;
                SourceVertex.UVIndex = Wedge.UVs.IsEmpty() ? -1 : SourceIndex;
                SourceVertex.NormalIndex = Mesh.bHasNormals ? SourceIndex : -1;
                Built.LODSourceVertices.Add(SourceVertex);
            }

            MeshIndices[Corner] = SharedIndex;
            const int32 TriangleIndex = Corner / 3;
            const int32 MaterialIndex = TriangleMaterialIndices[TriangleIndex];
            MaterialIndices[MaterialIndex].Add(VertexBase + SharedIndex);
        }

        // 정점을 공유한 뒤에는 이웃 면의 접선을 함께 누적한다.
        if (!MeshIndices.IsEmpty())
            Built.CalculateTangents(SharedVertices, MeshIndices);
        Built.Vertices.Append(SharedVertices);
        return true;
}
}

bool FFbxStaticMeshBuilder::Build(const FStaticMeshImportData& Source, FStaticMeshData& OutData)
{
    FStaticMeshData Built;

    // 재질 번호는 Importer가 모은 순서를 유지한다.
    for (const FImportedMaterial& Material : Source.Materials)
    {
        FStaticMaterialSlot Slot;
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
    int32 ControlPointBase = 0;

    for (const FImportedMesh& Mesh : Source.Meshes)
    {
        if (!AppendMesh(Mesh, Built, MaterialIndices, DefaultMaterialIndex, ControlPointBase))
            return false;

        ControlPointBase += Mesh.Points.Num();
    }

    // 같은 재질의 삼각형을 연속 배치해 기존 DrawIndexed 섹션을 만든다.
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialIndices.Num(); ++MaterialIndex)
    {
        const TArray<uint32>& Indices = MaterialIndices[MaterialIndex];
        if (Indices.IsEmpty())
            continue;

        FStaticMeshSection Section;
        Section.StartIndex = static_cast<uint32>(Built.Indices.Num());
        Section.IndexCount = static_cast<uint32>(Indices.Num());
        Section.MaterialSlotIndex = static_cast<uint32>(MaterialIndex);
        Built.Sections.Add(Section);
        Built.Indices.Append(Indices);
    }

    FString Error;
    if (!Built.Validate(Error))
    {
        HTR_LOG(Error, "[FBX Static Build] {}", Error);
        return false;
    }

    const FVector& FirstPosition = Built.Vertices[Built.Indices[0]].Position;
    Built.AABB = FBox{ FirstPosition, FirstPosition };
    for (uint32 Index : Built.Indices)
    {
        const FVector& Position = Built.Vertices[Index].Position;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            Built.AABB.Min[Axis] = std::min(Built.AABB.Min[Axis], Position[Axis]);
            Built.AABB.Max[Axis] = std::max(Built.AABB.Max[Axis], Position[Axis]);
        }
    }

    OutData = std::move(Built);
    return true;
}
