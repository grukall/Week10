#include "EnginePCH.h"
#include "Script/LuaBindings.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Color.h"
#include <sol/sol.hpp>

namespace
{
	// FColor는 uint8이라 Lua number를 그대로 넣으면 범위를 넘을 때 값이 돌아간다
	uint8 ToByte(int32 Value)
	{
		return static_cast<uint8>(std::clamp(Value, 0, 255));
	}
}

void RegisterLuaMathBindings(sol::state& Lua)
{
	// 값 타입이다. Lua로 넘어가면 복사본이 userdata로 만들어진다.
	// X·Y·Z는 union 안의 익명 구조체 멤버라 멤버 포인터 대신 람다로 접근한다.
	// sol2는 생성자를 FVector.new(...)로만 등록하므로, FVector(...)처럼 호출하려면 call_constructor로 넘긴다.
	// 아래 수학 타입들도 모두 같다.
	Lua.new_usertype<FVector>("FVector",
		sol::call_constructor, sol::constructors<FVector(), FVector(float, float, float)>(),

		"X", sol::property([](const FVector& V) { return V.X; }, [](FVector& V, float F) { V.X = F; }),
		"Y", sol::property([](const FVector& V) { return V.Y; }, [](FVector& V, float F) { V.Y = F; }),
		"Z", sol::property([](const FVector& V) { return V.Z; }, [](FVector& V, float F) { V.Z = F; }),

		"Length",     &FVector::Length,
		"Normalized", &FVector::Normalized,
		"Dot",        [](const FVector& A, const FVector& B) { return FVector::Dot(A, B); },
		"Cross",      [](const FVector& A, const FVector& B) { return FVector::Cross(A, B); },

		sol::meta_function::addition,    [](const FVector& A, const FVector& B) { return A + B; },
		sol::meta_function::subtraction, [](const FVector& A, const FVector& B) { return A - B; },
		sol::meta_function::unary_minus, [](const FVector& V) { return -V; },
		sol::meta_function::equal_to,    [](const FVector& A, const FVector& B) { return A == B; },
		// v * dt 와 dt * v 를 모두 받는다
		sol::meta_function::multiplication, sol::overload(
			[](const FVector& V, float F) { return V * F; },
			[](float F, const FVector& V) { return V * F; },
			[](const FVector& A, const FVector& B) { return A * B; }),
		sol::meta_function::division, [](const FVector& V, float F) { return V / F; },
		sol::meta_function::to_string, [](const FVector& V) { return std::format("({:.3f}, {:.3f}, {:.3f})", V.X, V.Y, V.Z); }
	);

	Lua.new_usertype<FRotator>("FRotator",
		sol::call_constructor, sol::constructors<FRotator(float, float, float)>(),

		"Pitch", sol::property([](const FRotator& R) { return R.Pitch; }, [](FRotator& R, float F) { R.Pitch = F; }),
		"Yaw",   sol::property([](const FRotator& R) { return R.Yaw; },   [](FRotator& R, float F) { R.Yaw = F; }),
		"Roll",  sol::property([](const FRotator& R) { return R.Roll; },  [](FRotator& R, float F) { R.Roll = F; }),

		sol::meta_function::to_string, [](const FRotator& R) { return std::format("(P={:.3f}, Y={:.3f}, R={:.3f})", R.Pitch, R.Yaw, R.Roll); }
	);

	// FVector와 같이 X·Y·Z·W가 union 안에 있어 람다로 접근한다
	Lua.new_usertype<FVector4>("FVector4",
		sol::call_constructor, sol::constructors<FVector4(), FVector4(float, float, float, float), FVector4(const FVector&, float)>(),

		"X", sol::property([](const FVector4& V) { return V.X; }, [](FVector4& V, float F) { V.X = F; }),
		"Y", sol::property([](const FVector4& V) { return V.Y; }, [](FVector4& V, float F) { V.Y = F; }),
		"Z", sol::property([](const FVector4& V) { return V.Z; }, [](FVector4& V, float F) { V.Z = F; }),
		"W", sol::property([](const FVector4& V) { return V.W; }, [](FVector4& V, float F) { V.W = F; }),

		sol::meta_function::addition,    [](const FVector4& A, const FVector4& B) { return A + B; },
		sol::meta_function::subtraction, [](const FVector4& A, const FVector4& B) { return A - B; },
		// 단항 -는 const 버전이 없어 복사본에 적용한다
		sol::meta_function::unary_minus, [](const FVector4& V) { FVector4 Copy = V; return -Copy; },
		sol::meta_function::equal_to,    [](const FVector4& A, const FVector4& B) { return A == B; },
		sol::meta_function::multiplication, sol::overload(
			[](const FVector4& V, float F) { return V * F; },
			[](float F, const FVector4& V) { return V * F; },
			[](const FVector4& A, const FVector4& B) { return A * B; }),
		sol::meta_function::division, [](const FVector4& V, float F) { return V / F; },
		sol::meta_function::to_string, [](const FVector4& V) { return std::format("({:.3f}, {:.3f}, {:.3f}, {:.3f})", V.X, V.Y, V.Z, V.W); }
	);

	Lua.new_usertype<FLinearColor>("FLinearColor",
		// A는 기본값이 1이라 RGB만 넘겨도 된다
		sol::call_constructor, sol::constructors<FLinearColor(), FLinearColor(float, float, float), FLinearColor(float, float, float, float), FLinearColor(const FColor&)>(),

		"R", &FLinearColor::R,
		"G", &FLinearColor::G,
		"B", &FLinearColor::B,
		"A", &FLinearColor::A,

		"ToFColor", &FLinearColor::ToFColor,

		sol::meta_function::multiplication, sol::overload(
			[](const FLinearColor& C, float F) { return C * F; },
			[](float F, const FLinearColor& C) { return C * F; }),
		sol::meta_function::to_string, [](const FLinearColor& C) { return std::format("(R={:.3f}, G={:.3f}, B={:.3f}, A={:.3f})", C.R, C.G, C.B, C.A); }
	);

	Lua.new_usertype<FColor>("FColor",
		sol::call_constructor, sol::factories(
			[]() { return FColor(); },
			[](int32 R, int32 G, int32 B) { return FColor(ToByte(R), ToByte(G), ToByte(B)); },
			[](int32 R, int32 G, int32 B, int32 A) { return FColor(ToByte(R), ToByte(G), ToByte(B), ToByte(A)); }),

		// 0~255 범위로 잘라서 넣는다
		"R", sol::property([](const FColor& C) { return static_cast<int32>(C.R); }, [](FColor& C, int32 V) { C.R = ToByte(V); }),
		"G", sol::property([](const FColor& C) { return static_cast<int32>(C.G); }, [](FColor& C, int32 V) { C.G = ToByte(V); }),
		"B", sol::property([](const FColor& C) { return static_cast<int32>(C.B); }, [](FColor& C, int32 V) { C.B = ToByte(V); }),
		"A", sol::property([](const FColor& C) { return static_cast<int32>(C.A); }, [](FColor& C, int32 V) { C.A = ToByte(V); }),

		"ReinterpretAsLinear", &FColor::ReinterpretAsLinear,

		sol::meta_function::to_string, [](const FColor& C) { return std::format("(R={}, G={}, B={}, A={})", C.R, C.G, C.B, C.A); }
	);

	Lua.new_usertype<FTransform>("FTransform",
		// 생성자 인자 순서는 C++과 같다: 위치, 스케일, 회전
		sol::call_constructor, sol::constructors<FTransform(), FTransform(FVector, FVector, FRotator)>(),

		// 멤버 변수를 직접 바인딩해서 t.Location.X = 1 처럼 트랜스폼 안의 값을 바로 고칠 수 있다
		"Location", &FTransform::Location,
		"Scale",    &FTransform::Scale,
		"Rotation", &FTransform::Rotation,

		"GetForward", &FTransform::GetForward,
		"GetUp",      &FTransform::GetUp,
		"GetRight",   &FTransform::GetRight,

		sol::meta_function::to_string, [](const FTransform& T) {
			return std::format("(Location=({:.3f}, {:.3f}, {:.3f}), Rotation=(P={:.3f}, Y={:.3f}, R={:.3f}), Scale=({:.3f}, {:.3f}, {:.3f}))",
				T.Location.X, T.Location.Y, T.Location.Z, T.Rotation.Pitch, T.Rotation.Yaw, T.Rotation.Roll, T.Scale.X, T.Scale.Y, T.Scale.Z);
		}
	);
}
