#pragma once

#include "BillboardComponent.h"

// ParticleSubUVShader의 b1
struct FSubUVConstants
{
	float CurrentFrame;
	float AtlasRowSize;
	float AtlasColSize;
	float Alpha;
};

struct FParticle
{
	// 파티클 한 개의 물리·수명·SubUV 프레임 상태를 담는다.
	FVector Location;
	FVector Velocity;
	float LifeTime;

	float Age;
	float Scale;
	uint32 SubUVFrame;
	float FrameTimer;

	float Alpha;

	bool bAlive;
};

struct FEffectDesc
{
	FString TexturePath;

	uint32 Cols = 8;
	uint32 Rows = 8;
	uint32 FrameCount = 0; 
	float FrameRate = 12.0f;
	bool bRandomStartFrame = true;      // false면 0번 프레임부터 재생

	int32 ParticleCount = 10;
	float MinLifeTime = 6.0f;
	float MaxLifeTime = 8.0f;

	// 축마다 이 범위에서 랜덤. 컴포넌트 회전으로 돌려서 방출한다.
	FVector MinVelocity = FVector(0.0f, 0.0f, 0.7f);
	FVector MaxVelocity = FVector(0.0f, 0.0f, 0.9f);
	FVector Acceleration = FVector(0.0f, 0.0f, 0.0f);

	float StartScale = 1.2f;
	float EndScale = 1.5f;
	float FadeStart = 0.8f; 

	// true: Activate 순간 ParticleCount개를 한꺼번에 방출한다.
	// false: 초당 ParticleCount / 평균 수명 개씩 꾸준히 방출한다.
	bool bBurst = false;
};

// 가장 단순한 SubUV 파티클입니다.
// Atlas 전체를 Col x Row로 나누고 CurrentFrame 하나만 변경합니다.
class UParticleSubUVComponent : public UBillboardComponent
{
	DECLARE_CLASS(UParticleSubUVComponent, UBillboardComponent)

	REFLECT_START(ClassName)
		PROPERTY(ColSize)
		PROPERTY(RowSize)
		PROPERTY(FrameRate)
		FUNCTION(SetSubUVSize)
		FUNCTION(SetFrameRate)
	REFLECT_END()

public:
	UParticleSubUVComponent();
	virtual ~UParticleSubUVComponent() = default;
	
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue) override;
	// SceneRenderer가 View별 거리 정렬 입력을 만들 수 있도록 현재 파티클 배열을 읽기 전용으로 제공한다.
	const TArray<FParticle>& GetParticlesForView() const { return Particles; }
	// 프레임의 첫 View 전에 파티클 인덱스별 상수를 준비하고 네 View에서 재사용한다.
	void BeginViewSubmission();
	// 불투명 파티클의 기존 정렬 경로를 선택한다.
	bool UsesOpaqueMaterial() const;
	// SceneRenderer가 정렬한 파티클을 View별 Billboard 행렬과 거리 순서로 렌더 큐에 넣는다.
	void SubmitParticleToRenderQueue(FRenderQueue& RenderQueue, int32 ParticleIndex, const FMatrix& WorldMatrix, float CameraDistanceSquared);

	void SetSubUVSize(uint32 Cols, uint32 Rows);
	void SetFrameRate(float InFrameRate);

	// 텍스처·아틀라스·튜닝 값을 바꾼다. 다음 Activate부터 반영된다.
	void ApplyDesc(const FEffectDesc& Desc);

	// 입자 배열을 새로 채우고 방출을 시작한다. bPrewarm이면 이미 흘러가던 상태로 시작한다 (연속 방출만).
	void Activate(bool bPrewarm = false);
	// 더 이상 방출하지 않는다. 살아 있는 입자는 수명대로 사라진다.
	void StopEmitting();
	// 모든 입자를 즉시 없애고 Tick을 끈다.
	void Deactivate();
	// 방출이 끝났고 살아 있는 입자가 없다.
	bool IsFinished() const { return !bEmitting && AliveCount == 0; }
	bool IsEmitting() const { return bEmitting; }

	void SetLoop(bool bInLoop) { bLoop = bInLoop; }
	bool IsLooping() const { return bLoop; }

	// BeginPlay에서 스스로 Activate(prewarm) 할지. 레벨에 놓인 이미터용이며 풀은 꺼내 쓸 때 Activate한다.
	void SetAutoActivate(bool bInAutoActivate) { bAutoActivate = bInAutoActivate; }

private:
	void RespawnParticle(FParticle& Particle);
	// 죽어 있는 입자 하나를 살린다. 없으면 false
	bool EmitOne();
	uint32 GetTotalFrames() const;
	float GetEmitRate() const;

	// Todo: Move to util class
	float GetRandomNumberBetween(float start, float end) const;
	float Lerp(float first, float second, float alpha) const;
	float GedSmoothStepedRatio(float start, float end, float value) const;

private:
	static const float MAX_NORMALIZED_VALUE;

	// ---- 튜닝 값 (ApplyDesc가 채운다. 기본값은 FEffectDesc 기본값과 같다) ----
	// 연속 방출: 초당 평균 방출량 = ParticleCount / 평균 수명. 수명 범위가 넓을수록 방출 간격이 더 불규칙해진다.
	int32 ParticleCount = 10;           // 동시에 존재할 수 있는 파티클 수 (Activate에서 반영)

	float MinLifeTime = 6.0f;           // 수명 최소값(초)
	float MaxLifeTime = 8.0f;          // 수명 최대값(초)

	FVector MinVelocity = FVector(0.0f, 0.0f, 0.7f);
	FVector MaxVelocity = FVector(0.0f, 0.0f, 0.9f);
	FVector Acceleration = FVector(0.0f, 0.0f, 0.0f);

	float StartScale = 1.2f;            // 생성 시 크기
	float EndScale = 1.5f;              // 수명이 끝날 때 크기

	float FadeStart = 0.8f;             // 수명 비율이 이 값을 넘으면 페이드아웃 시작 (0~1)

	uint32 FrameCount = 0;              // 0이면 ColSize * RowSize
	bool bRandomStartFrame = true;
	bool bBurst = false;

	// ---- 방출 상태 ----
	bool bAutoActivate = true;
	bool bLoop = true;                  // 연속 방출: 계속 방출 / 버스트: 죽은 입자를 바로 다시 방출
	bool bEmitting = false;
	float EmitAccumulator = 0.0f;       // 연속 방출에서 아직 내보내지 못한 입자 수 (소수)
	int32 EmittedCount = 0;             // 이번 Activate 이후 방출한 수. 루프가 아니면 ParticleCount에서 멈춘다
	int32 AliveCount = 0;

	TArray<FSubUVConstants> Constants;

	uint32 ColSize;
	uint32 RowSize;
	float FrameRate;

	// Todo: Reserve particles
	TArray<FParticle> Particles;
};
