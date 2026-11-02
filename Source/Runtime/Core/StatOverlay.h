#pragma once

#include "Types.h"
#include "EngineString.h"

// Overlay로 표시할 Stat 항목. 여러 항목을 동시에 켤 수 있어 비트 플래그로 둔다.
enum class EStatFlags : uint32
{
	None    = 0,
	FPS     = 1 << 0,
	Memory  = 1 << 1,
	Profile = 1 << 2,
	Decal   = 1 << 3,
};
DEFINE_ENUM_OPERATORS(EStatFlags)

// stat decal 오버레이에 보여 줄 데칼 수치. 렌더러가 채우고 에디터가 읽는다.
struct FDecalStats
{
	// 그리기 수치. 뷰마다 DrawDecals 시작에 0으로 되돌리므로 마지막에 그린 뷰의 값이 남는다.
	bool bMeshMethod = false;
	uint32 TotalDecals = 0;
	uint32 DrawnDecals = 0;		// 데칼 하나에 드로우 콜 하나다
	uint32 DrawnTriangles = 0;	// Mesh 방식에서 잘라 낸 메시의 삼각형 수

	// Mesh 방식 빌드 수치. 다시 만든 데칼이 있는 프레임에만 갱신해 마지막 빌드 결과를 남긴다.
	uint32 RebuiltDecals = 0;
	uint32 Receivers = 0;			// Broad phase(장면 BVH + AABB)를 통과한 받는 물체 수
	uint32 TrianglesTested = 0;		// 삼각형 BVH가 돌려준 삼각형 수
	uint32 TrianglesPassedSAT = 0;	// SAT로 박스와 겹친다고 나온 삼각형 수
	uint32 TrianglesClipped = 0;	// Clip 뒤에 만들어진 삼각형 수
	double BuildMs = 0.0;

	static FDecalStats& Get()
	{
		static FDecalStats Stats;
		return Stats;
	}
};

// 콘솔 stat 명령의 상태와 표시용 수치만 보관한다.
// 그리기는 Viewport를 소유한 에디터 쪽이 맡아 엔진이 ImGui에 의존하지 않게 한다.
class FStatOverlay
{
public:
	// 프레임마다 호출해 FPS 평균 창을 누적하고 표시 수치를 갱신한다.
	static void Tick(float DeltaTime);

	// "stat ..." 명령을 해석한다.
	// stat 명령이 아니면 false를 반환해 호출자가 나머지 명령 처리를 이어가게 한다.
	static bool ExecCommand(const FString& CommandLine, FString& OutMessage);

	static EStatFlags GetFlags() { return Flags; }
	static bool IsEnabled(EStatFlags Flag) { return HasFlag(Flags, Flag); }
	static bool IsAnyEnabled() { return Flags != EStatFlags::None; }

	// 샘플 구간 평균 FPS와 프레임 시간(ms). 매 프레임 값은 흔들려서 평균만 노출한다.
	static float GetFPS() { return DisplayFPS; }
	static float GetFrameTimeMs() { return DisplayFrameTimeMs; }

	// UObject::operator new/delete가 추적한 누적 할당량.
	static uint64 GetObjectAllocationBytes();
	static uint32 GetObjectAllocationCount();
	// 프로세스 전체가 점유한 물리 메모리(Working Set).
	static uint64 GetProcessWorkingSetBytes();

private:
	// 표시 수치 갱신 주기(초). 너무 짧으면 숫자가 읽히지 않는다.
	static constexpr float SampleInterval = 0.25f;

	inline static EStatFlags Flags = EStatFlags::None;
	inline static float AccumulatedTime = 0.0f;
	inline static int32 AccumulatedFrames = 0;
	inline static float DisplayFPS = 0.0f;
	inline static float DisplayFrameTimeMs = 0.0f;
};
