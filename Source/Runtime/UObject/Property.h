#pragma once

#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Transform.h"
#include "Math/Color.h"
#include "Core/NameTypes.h"

class UMaterial;
class UFont;

// 새 타입은 기존 값이 밀리지 않도록 끝에 추가한다
enum class EPropertyType { Unknown, Float, Int, String, Bool, Vector, Vector4, Rotator, Transform, Object, LinearColor, ByteColor, Name};

template <typename T>
constexpr EPropertyType GetPropertyType()
{
    if constexpr (std::is_pointer_v<T> &&
        std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
    {
        return EPropertyType::Object;
    }
    else
    {
        static_assert(sizeof(T) == 0, "Not Valid Type");
        return EPropertyType::Unknown;
    }
}

#define DEFINE_PROPERTY_TYPE(CppType, EnumValue)                    \
    template <> constexpr EPropertyType GetPropertyType<CppType>()  \
    { return EPropertyType::EnumValue; }

DEFINE_PROPERTY_TYPE(int, Int)
DEFINE_PROPERTY_TYPE(uint32, Int)
DEFINE_PROPERTY_TYPE(float, Float)
DEFINE_PROPERTY_TYPE(bool, Bool)
DEFINE_PROPERTY_TYPE(FString, String)
DEFINE_PROPERTY_TYPE(FVector, Vector)
DEFINE_PROPERTY_TYPE(FVector4, Vector4)
DEFINE_PROPERTY_TYPE(FTransform, Transform)
DEFINE_PROPERTY_TYPE(FRotator, Rotator)
DEFINE_PROPERTY_TYPE(FLinearColor, LinearColor)
DEFINE_PROPERTY_TYPE(FColor, ByteColor)
// FName은 이름 테이블의 번호라 FString과 메모리 구조가 다르다. String으로 매핑하면 안 된다.
DEFINE_PROPERTY_TYPE(FName, Name)

//DragFloat, DragInt, DragFloat3, DragFloat4, ColorEdit4 등 ImGui 위젯과 관련된 메타데이터를 저장하는 구조체
struct FProPertyWidgetMeta
{
	float MinValue;
	float MaxValue;
    float delta;
};

// 프로퍼티 지정자 (UE의 UPROPERTY 지정자에 대응)
enum EPropertyFlags : uint32
{
    CPF_None      = 0,
    CPF_Edit      = 1 << 0, // 디테일 패널에 노출
    CPF_EditConst = 1 << 1, // 노출하되 수정 불가
    CPF_Transient = 1 << 2, // 직렬화에서 제외
    CPF_LuaScriptPath = 1 << 3, // String 프로퍼티 옆에 .lua 파일 선택 버튼을 붙인다

    CPF_EditAnywhere    = CPF_Edit,
    CPF_VisibleAnywhere = CPF_Edit | CPF_EditConst,
    CPF_Default         = CPF_EditAnywhere,
};

struct FProperty
{
    FString Name;
    EPropertyType Type;
    size_t Offset;
    size_t Size;
    UClass* Class = nullptr;
    uint32 Flags = CPF_Default;

    bool HasAnyFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }
};