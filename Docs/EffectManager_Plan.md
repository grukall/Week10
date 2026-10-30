# 이펙트 매니저 구현 계획

목표 API (게임 로직은 Lua에서 호출)

```
SpawnEffect(이펙트, 위치, 회전, 루프 여부, 지속 시간)
SpawnEffectAttached(이펙트, 붙일 액터, 상대 트랜스폼, 루프 여부, 지속 시간)
SpawnTextEffect(텍스트, 위치, 색, 지속 시간)
```

사용처

| 상황 | 호출 |
|---|---|
| 팝콘을 받음 (반짝임) | `SpawnEffect` |
| 팝콘 아닌 것이 바닥/캐릭터에 부딪힘 (튀김 + "-3") | `SpawnEffect` + `SpawnTextEffect` |
| 먼작귀 발밑 연기 | `SpawnEffectAttached` (루프, 멈출 때 `StopEffect`) |

전체 흐름 한눈에

```
Lua (충돌 이벤트 등)
  └─ SpawnEffect(...)  ──►  FEffectManager (UWorld 멤버)
                               ├─ 풀에서 UParticleSubUVComponent 하나 꺼냄
                               │    └─ ApplyDesc → Activate
                               │         (시뮬레이션은 컴포넌트 자신의 TickComponent가 함)
                               ├─ 매 프레임 Tick: 끝난 이펙트를 풀로 반환
                               └─ 텍스트 이펙트: 매 프레임 HUD.AddWorldText (투영은 렌더 시점에 FSceneView로)
```

---

## 진행 상황 (중간점검 반영)

| 단계 | 상태 | 비고 |
|---|---|---|
| 1. 파티클 컴포넌트 | 완료 | 자동 점검 20개 통과 |
| 2. FEffectManager·풀·SpawnEffect | 완료 | 자동 점검 20개 통과 |
| 중간점검 수정 | 완료 | Init을 `NotifyBeginPlay` 맨 앞으로, 프리셋 표를 Init/Shutdown과 분리(생성자에서 기본 등록), 풀 액터 이름 고유화 |
| 3. SpawnEffectAttached·대상 파괴 처리 | 완료 | `TWeakObjectPtr`, 떼어내기는 `ReleaseSlot` 한 곳. 자동 점검 22개 통과 |
| 4. 텍스트 이펙트 | 완료 | `FHUD::AddWorldText` + `RenderHUD(…, View)`에서 투영, 렌더러에서 글자 폭으로 가운데 맞춤. 상승·페이드·처음 크게 시작·그림자. 게임 화면 캡처로 확인 (4-3) |
| 5. Lua 바인딩 | 완료 | 5개 함수 + `RegisterEffect(이름, 테이블)`(C++ 재빌드 없이 튜닝). 게임 스크립트 연결까지. Lua 점검 15개 통과, 실제 맵(GameJamMap0)에서 발밑 연기·튀김·`-3` 확인 (7장) |
| 6. Shutdown | 완료 | 2단계에서 처리. PIE 종료·Travel 모두 `EndPlay` → `ClearWorld` 순서 |
| 7. 실제 아틀라스·튜닝 | 1차 완료 | 레포에 있는 아틀라스로 배정: `PopcornCatch` = `StarParticle`(8x8 금색 반짝이), `HitSplash`·`FootSmoke` = `ParticleAtlas`(4x4, 작은 구→큰 구로 부푸는 16프레임). `CloudAtlas`는 크림색 바닥에서 어두워 뺐다. 수치는 GameJamMap0에서 1회 화면 확인 후 조정한 값이라, 2차 값은 플레이하며 확인 필요. 텍스트 크기 44 → 56 |

---

## 1. `UParticleSubUVComponent` 수정

지금 이 컴포넌트는 **끝나지 않는 이미터 한 종류**만 지원한다.

- `TickComponent`에서 `Age >= LifeTime`이면 무조건 `RespawnParticle` → 영원히 방출한다.
- 튜닝 값(`ParticleCount`, `MinLifeTime`, `MinRiseSpeed` …)은 private 하드코딩이고, `ParticleCount`는 생성자에서만 반영된다.
- 속도는 Z(위쪽)로만 나간다. 사방으로 튀는 이펙트를 만들 수 없다.
- `BeginPlay`가 입자마다 `Age`를 랜덤으로 줘서 "이미 흘러가던" 상태로 시작한다.

풀에서 꺼내 쓰려면 **한 번 터지고 끝나는 모드**, **끝났는지 알려주는 함수**, **실행 중에 설정을 바꾸는 함수**가 필요하다.

### 1-1. `FEffectDesc` 구조체

`SpawnEffect`의 "이펙트" 인자가 가리키는 것. 이펙트 한 종류를 만드는 데 필요한 값을 전부 담는다.

```cpp
struct FEffectDesc
{
    FString TexturePath;          // 아틀라스 텍스처
    uint32  Cols = 8, Rows = 8;   // 아틀라스 칸 수
    float   FrameRate = 12.0f;

    int32   ParticleCount = 10;
    float   MinLifeTime = 0.5f, MaxLifeTime = 1.0f;
    FVector MinVelocity, MaxVelocity;  // 축마다 이 범위에서 랜덤
    float   StartScale = 1.0f, EndScale = 1.0f;
    float   FadeStart = 0.8f;          // 수명 비율이 이 값을 넘으면 페이드아웃

    bool    bBurst = false;     // true: Activate 순간 ParticleCount개를 한꺼번에 방출
    bool    bRandomStartFrame;  // 지금처럼 시작 프레임을 랜덤으로 할지 (연기: true, 반짝임: false)
};
```

- 속도를 `float RiseSpeed`에서 `FVector` 범위로 바꾸는 게 핵심이다. 연기는 `Min=(−0.1,−0.1,0.7) Max=(0.1,0.1,0.9)`, 튀김은 `Min=(−2,−2,1) Max=(2,2,3)`처럼 쓴다.
- 튀는 이펙트에 중력이 필요하면 `FVector Acceleration`을 하나 더 두고 Tick에서 `Velocity += Acceleration * dt`를 하면 된다.
- 지금 하드코딩된 값을 기본 Desc로 옮겨두면 기존 `AParticleActor`가 그대로 동작한다.

### 1-2. `ApplyDesc(const FEffectDesc&)`

Desc 값을 컴포넌트에 넣는다.

```cpp
void UParticleSubUVComponent::ApplyDesc(const FEffectDesc& Desc)
{
    SetTexture(Desc.TexturePath);      // UBillboardComponent에서 상속
    SetSubUVSize(Desc.Cols, Desc.Rows);
    SetFrameRate(Desc.FrameRate);
    // 나머지 튜닝 값 복사
}
```

**`SetTexture`가 다른 이펙트에 영향을 주지 않는 이유** ([BillboardComponent.cpp:34](../Source/Runtime/Component/BillboardComponent.cpp))

- 생성자에서는 모든 파티클 컴포넌트가 `"SubUVMaterial"` 하나를 공유한다.
- `SetTexture`는 `Material->bIsInstance`가 false면 `UMaterial::CreateInstance`로 **자기 전용 복사본**을 만든 다음 그 복사본의 `Textures[0]`만 바꾼다.
- 풀의 컴포넌트는 처음 쓸 때 한 번만 복사본이 생기고, 그다음부터는 자기 복사본의 텍스처 포인터만 바뀐다 → 재사용할 때 할당이 없다.
- 반대로 `GetAssetByPath<UMaterial>("SubUVMaterial")->Textures[0]`를 **직접** 고치면 모든 파티클이 같이 바뀐다. 이렇게는 하지 말 것.
- `SetTexture(const FString&)`는 내부에서 `LoadTexture`를 부른다. AssetManager가 캐시하는지 확인하고, 아니라면 Desc를 등록할 때 `UTexture2D*`를 미리 로드해두고 포인터 버전을 쓴다.

### 1-3. `Activate()` / `StopEmitting()` / `IsFinished()` / `bLoop`

```cpp
void Activate();        // 입자 배열 초기화 + 방출 시작
void StopEmitting();    // 더 이상 Respawn하지 않음. 살아 있는 입자는 수명대로 사라짐
void Deactivate();      // 즉시 전부 bAlive=false (풀 반환용)
bool IsFinished() const;// 방출이 멈췄고 살아 있는 입자가 0개
bool bEmitting = false;
```

`Activate`가 할 일

1. `Particles.Reset(); Particles.Reserve(Desc.ParticleCount);`
   TArray는 `Reset`해도 용량을 유지한다. 그래서 몇 번 쓰고 나면 재할당이 일어나지 않는다.
2. 입자를 채운다.
   - `bBurst`: 전부 `RespawnParticle`로 살린다(Age = 0).
   - 연속 방출: `bAlive = false`로 넣어두고, Tick에서 방출 간격마다 하나씩 살린다. 아니면 지금처럼 전부 살리되 Age를 0~수명 사이로 흩는다.
3. `bEmitting = true`, `PrimaryComponentTick.SetTickFunctionEnable(true)`.

**`Activate`에서 입자 상태를 완성해야 하는 이유:** `SpawnEffect`는 보통 `UpdatePendingOverlaps`(충돌 이벤트) 안에서 불린다. 이건 `TickTaskManager`가 컴포넌트 Tick을 다 돌린 **뒤**다 ([World.cpp:117](../Source/Runtime/Engine/World.cpp)). 그래서 새로 켠 이펙트는 이번 프레임에 Tick 없이 바로 렌더된다. 입자 위치와 `bAlive`가 이전 사용 때 값으로 남아 있으면, 한 프레임 동안 엉뚱한 곳에 그려진다.

`TickComponent` 변경점

```cpp
if (Particle.Age >= Particle.LifeTime)
{
    if (bEmitting && bLoop) RespawnParticle(Particle);
    else                    Particle.bAlive = false;
    continue;
}
// bAlive == false인 입자는 갱신도 건너뜀
```

- `bLoop=false`이면 첫 수명이 끝난 입자는 다시 살아나지 않는다 → 결국 전부 죽고 `IsFinished()`가 true가 된다.
- `SceneRenderer`는 이미 `bAlive == false`인 입자를 건너뛴다 ([SceneRenderer.cpp:426](../Source/Runtime/Render/SceneRenderer.cpp)). 렌더 쪽은 고칠 게 없다.
- `IsFinished`는 Tick 안에서 살아 있는 입자 수를 세어두면 O(1)이 된다.

### 1-4. `bPrewarm` 분리

`AddComponent`는 컴포넌트를 만들자마자 `DispatchBeginPlay`를 부른다 ([Actor.cpp](../Source/Runtime/GameFramework/Actor.cpp)의 `AddComponent`). 지금 `BeginPlay`에 있는 랜덤 Age 채우기가 그대로 돌면, 풀에 넣는 순간 입자가 잔뜩 살아 있는 상태가 된다.

- `BeginPlay`의 입자 채우기를 `if (bPrewarm)`으로 감싼다.
- `AParticleActor`가 만든 컴포넌트는 `bPrewarm = true`, `bLoop = true`, `bEmitting = true`로 둔다. 그러면 지금과 똑같이 보인다.
- 풀 컴포넌트는 `bPrewarm = false`라 `BeginPlay`에서 입자를 만들지 않는다.

### 1-5. (선택) 컬링 범위 넓히기

화면에 그릴지는 **입자가 아니라 컴포넌트 단위**로 정한다.

- 컴포넌트의 범위 상자는 `UBillboardComponent::CalcLocalBounds`가 만드는데, 컴포넌트 위치에 사각형 하나 크기다 ([BillboardComponent.cpp:76](../Source/Runtime/Component/BillboardComponent.cpp)).
- 입자는 방출 지점에서 `Velocity * 시간`만큼 월드 공간에서 혼자 움직인다. 범위 상자는 이를 모른다.
- 상자가 화면 밖이면 입자가 화면 안에 있어도 **전부** 안 그려진다.

이번 이펙트들은 퍼지는 범위가 작아서 보통은 문제가 없다. 발밑 연기 꼬리가 화면 가장자리에서 한꺼번에 사라지는 게 거슬리면, `CalcLocalBounds`를 재정의해서 `max(|Velocity|) * MaxLifeTime`만큼 상자를 키운다.

### 1단계 확인

- 기존 ParticleActor 레벨을 열었을 때 예전과 똑같이 보이는지
- 임시 액터에 컴포넌트 하나를 붙이고 `bLoop=false, bBurst=true`로 `Activate` → 한 번 터지고, 다 사라진 뒤 `IsFinished()`가 true가 되는지
- 서로 다른 텍스처를 준 컴포넌트 두 개가 각자 다른 아틀라스로 보이는지

---

## 2. `FEffectManager` 본체

### 2-1. 어디에 둘까: `UWorld` 멤버

`FHUD HUD;`처럼 `FEffectManager EffectManager;`를 `UWorld`에 둔다.

- **싱글톤으로 만들면 안 되는 이유:** 에디터 월드와 PIE 월드가 동시에 존재한다. 싱글톤이면 두 월드가 풀 하나를 같이 쓰게 되고, PIE를 끌 때 PIE 월드의 컴포넌트를 들고 있는 채로 남는다.
- `UObject`일 필요가 없다. 직렬화도 PIE 복제도 필요 없는 실행 중 상태라서 일반 클래스면 충분하다.
- 접근자: `FEffectManager& GetEffectManager() { return EffectManager; }`

### 2-2. 풀 액터와 `Init`

**컴포넌트에 소유 액터가 꼭 필요한 이유**

- `UParticleSubUVComponent::SubmitToRenderQueue`가 `GetOwner()->GetWorld()`를 쓴다.
- Scene 등록(`Scene.AddPrimitive`)과 Tick 등록이 `AActor::AddComponent` 안에서 이루어진다.

그래서 매니저가 **숨은 액터 하나**를 만들고 컴포넌트를 전부 그 액터에 붙여둔다.

```cpp
void FEffectManager::Init(UWorld* InWorld)
{
    World = InWorld;
    PoolOwner = World->SpawnActor<AActor>(FName("EffectPool"));
    for (int32 i = 0; i < InitialPoolSize; ++i)
        AddPooledComponent();      // 아래
}

void FEffectManager::AddPooledComponent()
{
    auto* Comp = Cast<UParticleSubUVComponent>(
        PoolOwner->AddComponent(UParticleSubUVComponent::StaticClass()));
    Comp->Deactivate();            // 입자 전부 bAlive=false
    Comp->PrimaryComponentTick.SetTickFunctionEnable(false);
    // 슬롯 추가, FreeSlots에 넣기
}
```

- **부르는 시점:** `UWorld::NotifyBeginPlay`의 **맨 앞**에서 `if (IsGameWorld()) EffectManager.Init(this);`. 액터 순회보다 먼저 해야 액터·스크립트의 `BeginPlay`에서 `SpawnEffect`를 부를 수 있다. 순회가 매번 `Actors.Num()`을 다시 읽으므로 풀 액터도 같은 순회에서 BeginPlay를 받는다. 에디터 월드에서는 만들지 않는다 → 에디터 레벨에 풀 액터가 저장될 일이 없다.
- **풀 액터 이름은 고유하게** (`MakeUniqueObjectName(..., "EffectPool")`). 이 엔진의 `NewObject`는 같은 Outer에 같은 이름이 있으면 기존 객체를 그대로 돌려준다. 고정 이름이면 같은 월드에서 다시 `Init`할 때 한 액터가 레벨에 두 번 등록되고, `ClearWorld`가 두 번 delete한다(디버그 힙 assert로 확인함).
- `AddComponent`는 끝에서 `RegisterAllActorTickFunctions(true)`를 부른다. 그래서 Tick 끄기는 반드시 `AddComponent` **다음에** 한다.
- 풀 액터에 루트 컴포넌트가 없으면 `USceneComponent` 하나를 루트로 만들어둔다. 붙였던 이펙트를 반환할 때 여기로 다시 붙인다.
- `InitialPoolSize`는 동시에 보일 최대 개수 정도로 잡는다. 팝콘 낙하 간격이 0.5–1.2초이고 이펙트 수명이 1초 남짓이면 16개 정도로 충분하다.

### 2-3. 비활성 컴포넌트 처리

풀에서 쉬는 컴포넌트도 Scene에는 primitive로 등록된 채로 있다. 두 가지를 해둔다.

| 처리 | 이유 |
|---|---|
| `PrimaryComponentTick.SetTickFunctionEnable(false)` | 쉬는 동안 입자 루프를 돌리지 않음 |
| 입자 전부 `bAlive=false` (`Deactivate`) | 컬링을 통과해도 `SceneRenderer`가 그릴 입자가 없음 |

`SetHiddenInGame(true)`도 같이 걸어둘 수 있지만, SceneRenderer가 이 플래그를 검사하는지는 확인하지 않았다. `bAlive`는 코드에서 확인한 확실한 경로다.

### 2-4. 이펙트 프리셋 등록

```cpp
TMap<FName, FEffectDesc, FNameHash> Descs;   // FName은 기본 std::hash가 없어 FNameHash를 넘긴다
void RegisterDesc(FName Name, const FEffectDesc& Desc);
```

- `Init`에서 `"PopcornCatch"`, `"HitSplash"`, `"FootSmoke"` 등을 코드로 등록한다. 나중에 필요하면 JSON으로 빼도 된다.
- Lua에서는 이름 문자열로 부른다. 없는 이름이면 로그를 남기고 무효 핸들을 돌려준다.

### 2-5. 슬롯 상태 관리

```cpp
enum class EEffectSlotState : uint8 { Free, Active, Stopping };

struct FEffectSlot
{
    UParticleSubUVComponent* Comp = nullptr;
    EEffectSlotState State = EEffectSlotState::Free;
    uint32 Generation = 0;
    bool   bLoop = false;
    float  Remaining = -1.0f;     // 남은 방출 시간. 음수 = 무한
    AActor* AttachTarget = nullptr;
};
TArray<FEffectSlot> Slots;
TArray<int32> FreeSlots;
```

```
Free ──Spawn──► Active ──(시간 끝 / StopEffect / 한 번만 모드)──► Stopping ──IsFinished──► Free
                  │                                                                 ▲
                  └────────────── StopEffect(bImmediate=true) ──────────────────────┘
```

- **Active:** 방출 중.
- **Stopping:** `StopEmitting()`을 부른 상태. 살아 있는 입자가 수명대로 사라지길 기다린다. 이 단계가 없으면 이펙트가 뚝 끊겨 보인다.
- **Free:** `Deactivate()` + Tick 끄기 + `Generation++` + `FreeSlots`에 반환.

### 2-6. 핸들과 `Generation`

```cpp
struct FEffectHandle { int32 Index = -1; uint32 Generation = 0; bool IsValid() const { return Index >= 0; } };
```

**`Generation`이 막아주는 상황**

1. 발밑 연기를 스폰해서 핸들 `{3, 7}`을 받는다.
2. 무언가 때문에 슬롯 3이 반환되고, 다른 이펙트가 슬롯 3을 다시 쓴다(세대 8).
3. 나중에 Lua가 옛 핸들 `{3, 7}`로 `StopEffect`를 부른다 → 세대가 다르니 무시한다. 남의 이펙트를 끄지 않는다.

`StopEffect`나 `IsEffectActive` 같은 핸들 함수는 모두 `Slots[Index].Generation == Handle.Generation`부터 확인한다.

### 2-7. 풀이 바닥났을 때

하나를 고른다.

| 정책 | 장점 | 단점 |
|---|---|---|
| 최대치까지 늘리기 (`AddPooledComponent`) | 이펙트가 빠지지 않음 | 게임 중에 `NewObject` 발생 |
| 가장 오래된 Active/Stopping 빼앗기 | 메모리 고정 | 오래된 이펙트가 뚝 끊김 |
| 무시하고 무효 핸들 반환 | 가장 단순 | 이펙트가 안 나옴 |

추천: **최대치까지 늘리고, 그 이상이면 무시하면서 로그를 남긴다.** 로그가 찍히면 `InitialPoolSize`를 올린다.

---

## 3. Spawn API

### 3-1. `SpawnEffect`

```cpp
FEffectHandle SpawnEffect(FName Effect, const FVector& Location, const FRotator& Rotation, bool bLoop, float Duration);
```

1. Desc를 찾고 → 빈 슬롯을 하나 꺼낸다.
2. `Comp->SetWorldLocation(...)`와 회전을 설정한다.
3. `Comp->ApplyDesc(Desc)`, `Comp->bLoop = bLoop`, `Comp->Activate()`
4. 슬롯 `State=Active`, `Remaining`을 설정하고 `{Index, Generation}`을 반환한다.

**회전 인자의 의미:** 입자는 항상 카메라를 보는 사각형이다(`View.BuildBillboardMatrix`). 컴포넌트를 돌려도 입자 모양은 돌지 않는다. 회전은 **방출 방향**에만 의미가 있다. `RespawnParticle`에서 랜덤 속도를 컴포넌트 회전으로 돌려서 쓰면 된다. 예: 벽에 맞았을 때 벽 법선 쪽으로 튀게.

**순서 주의:** 위치를 먼저 설정하고 `Activate`를 불러야 한다. `RespawnParticle`이 `GetWorldLocation()`을 그 순간 읽는다.

### 3-2. `SpawnEffectAttached`

```cpp
FEffectHandle SpawnEffectAttached(FName Effect, AActor* Target, const FTransform& Relative, bool bLoop, float Duration);
```

1. 슬롯을 꺼낸다.
2. `Comp->SetupAttachment(Target->GetRootComponent(), EAttachmentRule::KeepRelative)`
3. 상대 위치·회전을 설정한다(발밑이면 `(0, 0, -캐릭터 반높이)` 정도).
4. `ApplyDesc` → `Activate`, `Slot.AttachTarget = Target`

**다른 액터의 컴포넌트에 붙여도 되는 이유 (코드 확인함)**

- 부착 관계는 액터가 아니라 **컴포넌트끼리**의 `AttachParent` / `AttachChildren` 포인터다.
- `USceneComponent::MarkTransformDirty`는 소유 액터와 상관없이 `AttachChildren` 전부에 전파된다.
- `GetWorldMatrix()`는 `AttachParent`를 따라 올라가며 계산한다.
- 그래서 소유는 풀 액터여도 위치는 대상 캐릭터를 따라간다. `GetOwner()->GetWorld()`도 풀 액터의 월드라 문제가 없다.

**입자는 따라오지 않는다:** 컴포넌트(방출 지점)는 따라가지만, 이미 나온 입자는 방출 순간의 월드 좌표에서 혼자 움직인다. 발밑 연기에는 이게 자연스러운 꼬리가 된다.

**반환할 때:** `Comp->SetupAttachment(PoolOwner->GetRootComponent(), ...)`로 다시 붙이고 `AttachTarget = nullptr`. 대상 액터가 파괴될 때의 처리는 5번에서.

### 3-3. `StopEffect`

```cpp
void StopEffect(FEffectHandle Handle, bool bImmediate = false);
```

- 세대가 다르면 무시한다.
- `bImmediate=false`: `Comp->StopEmitting()`, `State=Stopping`. 남은 연기가 자연스럽게 사라진다.
- `bImmediate=true`: 그 자리에서 Free 처리(5번의 `ReleaseSlot`).

### 3-4. `bLoop` / `Duration` 조합

| 인자 | 동작 | 예 |
|---|---|---|
| `bLoop=false` | 한 번 방출. 입자가 다 죽으면(`IsFinished`) 반환. `Duration`은 무시하거나 방출 시간 상한으로만 사용 | 반짝임, 튀김 |
| `bLoop=true, Duration>0` | `Duration`초 동안 방출 → Stopping → 다 죽으면 반환 | 3초간 연기 |
| `bLoop=true, Duration<=0` | `StopEffect`를 부를 때까지 방출 | 걷는 동안 발밑 연기 |

**Duration이 끝났다고 바로 반환하면 안 된다.** 그 순간 살아 있는 입자가 한 프레임 만에 사라진다. Duration은 "방출을 언제 멈추냐"이고, 반환은 "입자가 다 죽었냐"로 판단한다.

---

## 4. `SpawnTextEffect`: HUD로 그리기

### 4-1. HUD를 쓰는 이유

- `UTextRenderComponent`(월드 공간 텍스트)는 **게임에서 그려지지 않는다.** [EditorEngine.cpp:730](../Source/Editor/HitoriEd/EditorEngine.cpp)에서 `TObjectIterator`로 그리는 게 전부이고, 런타임 렌더 파이프라인에는 텍스트 단계가 없다.
- HUD는 게임에서 그려진다(`FSceneRenderPipeline::RenderHUD`). 알파 블렌딩이 켜져 있고 깊이 검사를 안 해서 항상 맨 위에 보인다 ([HUDRenderer.cpp:21](../Source/Runtime/Render/HUDRenderer.cpp), 코드 확인함). `Color.A`로 페이드가 된다.
- **HUD는 매 프레임 비워진다.** `UWorld::Tick` 시작에서 `HUD.BeginFrame()` → `Commands.Reset()`. 그래서 "-3"을 1초 띄우려면 1초 동안 매 프레임 `AddText`를 불러야 한다. 이 상태를 들고 있는 곳이 이펙트 매니저다.

### 4-2. 데이터

```cpp
struct FTextEffect
{
    FString Text;
    FVector WorldLocation;
    FLinearColor Color;
    float Age = 0.0f;
    float Duration = 1.0f;
};
TArray<FTextEffect> TextEffects;
```

`SpawnTextEffect`는 `TextEffects.Add(...)`만 한다. 그리는 일은 `Tick`이 한다.

### 4-3. 매 프레임 갱신 (`Tick` 안에서) — 중간점검 후 변경

> **변경:** 처음에는 매니저 `Tick`에서 카메라 행렬로 화면 좌표를 구하려 했지만, 실제 화면은 그 행렬로 그려지지 않는다.
> 게임·PIE 화면은 `FGameViewportClient::CreateSceneView`가 만든 `FSceneView`로 렌더되는데, 이 함수는 Roll을 없앤
> 자체 회전 기저를 만들고 화면비도 뷰포트 크기에서 가져온다. 카메라 컴포넌트의 `GetViewProjectionMatrix()`는
> 카메라 자체 회전과 `AspectRatio` 멤버(기본 16:9)를 쓰므로 두 행렬이 다르고, 글자가 대상에서 벗어난다.
> 그래서 **투영은 렌더 시점에 그 뷰포트의 `FSceneView`로 한다.**

매니저가 하는 일 (수명만)

```
for 각 텍스트 이펙트 (뒤에서부터, 지우기 쉽게):
    Age += dt
    Age >= Duration 이면 RemoveAtSwap, continue
    WorldLocation.Z += RiseSpeed * dt                       // 위로 떠오름
    Alpha = 1 - smoothstep(FadeStart, 1, Age / Duration)    // 끝부분에서 사라짐
    HUD.AddWorldText(Text, WorldLocation, FontSize, Color with A = Alpha)
```

HUD·렌더 쪽에 추가할 것

- `FHUDDrawCommand`에 월드 좌표 텍스트 종류(`WorldText`)와 `FVector WorldPosition`을 추가하고, `FHUD::AddWorldText`를 만든다.
- `FSceneRenderPipeline::RenderHUD`가 `const FSceneView&`를 받아, 월드 텍스트를 `View.ViewProjection`과 뷰포트 크기로 화면 좌표로 바꾼 뒤 그린다. 두 호출 지점(`EditorEngine.cpp`의 PIE HUD, `GameEngine.cpp`) 모두 그 자리에서 View를 쓸 수 있다.
- 카메라 뒤(clip W ≤ 0)면 그리지 않는다.
- 렌더 결과는 픽셀 단위다. HUD 렌더러는 기준 해상도 단위에 `GetScale()`을 곱하므로, 픽셀을 배율로 나눠 넣거나 월드 텍스트만 배율을 건너뛴다.

이렇게 하면 렌더된 화면과 정확히 맞고, 화면비 보정이 필요 없고, 화면 분할을 해도 뷰포트마다 맞게 그려진다.

**가운데 맞춤:** 렌더러는 글자를 배치하면서 폭을 알기 때문에, 월드 텍스트에 가운데 맞춤 플래그를 두고 렌더러에서 폭의 절반만큼 당기면 된다. 글자 수로 어림하면 `FString::size()`가 UTF-8 바이트 수라 한글이 섞이면 틀린다.

**외곽선/그림자:** HUD 텍스트에는 외곽선이 없다. 검은색으로 (+2, +2) 어긋나게 먼저 한 번 그리고, 원래 색으로 다시 그리면 그림자처럼 보인다.

### 4-4. HUD 방식의 한계 (알고 쓰기)

- 거리에 따라 크기가 변하지 않는다. 필요하면 카메라 거리로 FontSize를 조절한다.
- 벽 뒤에 있어도 보인다(깊이 검사 없음). 데미지 숫자라면 보통 이게 맞다.
- HUD 명령 목록은 월드당 하나지만, 월드 텍스트는 뷰포트마다 그 View로 투영하므로 화면 분할에도 맞는다.

---

## 5. `FEffectManager::Tick`

### 5-1. 부를 위치

`UWorld::Tick`의 게임 월드 블록 안에서, **Lua 코루틴 다음, `FlushPendingKill` 앞**.

```
UWorld::Tick
  Scene.UpdateAllTransforms()
  HUD.BeginFrame()               ← HUD 명령 비움
  BeginPlayList 처리
  TickTaskManager.RunAllTickGroups()   ← 액터·카메라 이동, 파티클 컴포넌트 시뮬레이션
  UpdatePendingOverlaps()        ← 충돌 이벤트 → 여기서 SpawnEffect가 많이 불림
  TickScriptCoroutines()
  EffectManager.Tick(DeltaTime)  ← ★ 여기
  FlushPendingKill()             ← 파괴된 객체 실제 delete
```

이 위치여야 하는 이유

| 이유 | 설명 |
|---|---|
| HUD 명령이 살아남음 | `BeginFrame` 뒤라 이번 프레임에 그려진다 |
| 텍스트가 떨리지 않음 | 카메라와 대상이 이번 프레임 이동을 끝낸 뒤 화면 좌표를 계산한다 |
| 파괴된 대상을 잡을 수 있음 | `DestroyActor`는 즉시 `RF_PendingKill`만 표시하고 실제 delete는 `FlushPendingKill`에서 한다. 그 사이라서 `IsPendingKill()`로 확인할 수 있고 메모리도 아직 유효하다 |

### 5-2. 할 일

```
for 각 슬롯 (Free 제외):
    // (a) 붙은 대상 파괴 처리 (구현: TWeakObjectPtr<AActor>, Destroy 예정이면 IsValid()가 false)
    if bAttached && !AttachTarget.IsValid():
        풀 액터 루트로 다시 붙임 (KeepWorld)
        Active였다면 StopEmitting → Stopping

    // (b) 방출 시간
    if Active && Remaining >= 0:
        Remaining -= dt
        if Remaining <= 0: StopEmitting → Stopping
    if Active && !bLoop: Stopping으로 (한 번만 모드는 Activate 직후부터 끝나길 기다리면 됨)

    // (c) 반환
    if Stopping && Comp->IsFinished(): ReleaseSlot(i)

텍스트 이펙트 갱신 (4-3)
```

**(a)가 꼭 필요한 이유 (코드 확인함)**

- `UWorld::DestroyActor`는 대상 액터 **자기 컴포넌트**의 프록시·Tick만 정리한다. 다른 액터 소유인 이펙트 컴포넌트가 붙어 있어도 떼어내지 않는다.
- ~~delete된 부모를 가리켜 크래시가 난다~~ → **정정:** `USceneComponent`의 소멸자가 자식들의 `AttachParent`를 nullptr로 바꾸므로 크래시는 나지 않는다.
- 대신 부모가 사라지는 순간 부모 기준이던 상대 위치가 그대로 월드 위치가 되어, 이펙트가 월드 원점 근처로 튀고 루프 이펙트는 거기서 계속 방출된다.
- 그래서 `FlushPendingKill` 전에 떼어낸다. `KeepWorld`를 쓰면 그 자리에 남아서 남은 연기가 자연스럽게 사라진다.

`ReleaseSlot(i)`

```cpp
Comp->Deactivate();
Comp->PrimaryComponentTick.SetTickFunctionEnable(false);
if (AttachTarget) { 풀 루트로 다시 붙임; AttachTarget = nullptr; }
Slot.State = Free; ++Slot.Generation; FreeSlots.Add(i);
```

---

## 6. 정리 (`Shutdown`)

- **부르는 곳:** `UWorld::EndPlay`, `UWorld::ClearWorld`
- **할 일**
  - `Slots`, `FreeSlots`, `TextEffects`를 비운다.
  - `PoolOwner = nullptr`. 풀 액터 자체는 월드가 정리하면서 지운다. 이미 레벨과 함께 지워질 거라면 `DestroyActor`를 또 부르지 않는다.
  - `World = nullptr`
- `Init`을 두 번 부르는 경우(PIE 재시작 등)를 대비해, `Init` 앞에서 이미 초기화돼 있으면 무시하거나 `Shutdown`부터 부른다.
- 이전 플레이의 핸들은 슬롯이 비워졌으니 `Index` 범위 검사에서 걸러진다. 범위 검사를 빼먹지 말 것.

---

## 7. Lua 바인딩: `LuaBindings_Effect.cpp`

[LuaBindings_Effect.cpp](../Source/Runtime/Script/LuaBindings_Effect.cpp) (구현 완료). [LuaBindings_HUD.cpp](../Source/Runtime/Script/LuaBindings_HUD.cpp)와 같은 방식으로, 스크립트 environment마다 `Owner`를 잡아 그 월드의 매니저를 찾는다. 게임 월드가 아니면 아무것도 하지 않는다.

### Lua API

```lua
local h = SpawnEffect(name, location [, rotation [, loop [, duration]]])           -- 실패하면 -1
local h = SpawnEffectAttached(name, actor [, offset [, loop [, duration [, rotation]]]])
SpawnTextEffect(text, location [, color [, duration]])
StopEffect(h [, immediate])         -- immediate가 아니면 남은 입자가 사라진 뒤 정리
IsEffectActive(h)                   -- 재생 중이거나 남은 입자가 사라지는 중이면 true

-- 키 이름은 FEffectDesc 필드와 같다. Base가 있으면 그 프리셋에서 시작한다. 같은 이름은 덮어쓴다.
RegisterEffect("BigSplash", {
    Base = "HitSplash",
    ParticleCount = 20,
    MinVelocity = FVector(-2, -2, 2), MaxVelocity = FVector(2, 2, 4),
    Acceleration = FVector(0, 0, -8),
})
```

- **핸들:** 숫자 하나 (`FEffectHandle::Pack`). LuaJIT는 숫자를 double로 들고 있어 2^53까지만 정확하므로, 처음 계획한 `Generation << 32 | Index` 대신 `Generation * 65536 + Index`로 넣는다(Index는 MaxPoolSize 미만). 무효면 `-1`.
- **RegisterEffect 검사:** 타입이 틀리거나(`MinVelocity = 3`), `Cols`/`Rows`가 1 미만이거나, 없는 `Base`면 어느 키인지 알려 주는 Lua 에러를 낸다. 모르는 키는 오타일 가능성이 커서 경고 로그만 남기고 등록한다.
- **주의:** 이 함수들은 스크립트 environment에 등록된다. `dofile`로 읽은 라이브러리(`Lib/*.lua`)에서는 보이지 않으므로, 라이브러리는 상태만 알려 주고(`Controller.IsMoving()`) 호출은 액터 스크립트에서 한다.

### 연결한 곳

| 위치 | 호출 |
|---|---|
| `PopcornSpawner.lua` `Collect` — 팝콘을 받았을 때 | `SpawnEffect("PopcornCatch", 낙하물 위치)` |
| `PopcornSpawner.lua` `Collect` — 팝콘이 아닌 것에 맞았을 때 | `SpawnEffect("HitSplash", 낙하물 위치)` + `SpawnTextEffect("-3", 플레이어 위치 + 1.4, 분홍)` (표시만, 점수는 따로) |
| `PopcornSpawner.lua` `Tick` — 팝콘이 아닌 것이 바닥에 처음 닿을 때 | `SpawnEffect("HitSplash", 바닥 위치)` |
| `Player1.lua` / `Player2.lua` `Tick` — 걷기 시작 / 멈춤 | `SpawnEffectAttached("FootSmoke", obj, FVector(0, 0, -0.5), true)` / `StopEffect(h)`. 이동 여부는 `Lib/PlayerController.lua`의 `Controller.IsMoving()` |

---

## 구현 순서와 단계별 확인

| 순서 | 작업 | 확인 |
|---|---|---|
| 1 | 컴포넌트 수정 (1-1 ~ 1-4) | 기존 ParticleActor 그대로 / 한 번만 모드에서 `IsFinished` |
| 2 | 매니저 뼈대 + 풀 + `SpawnEffect` + Tick 반환 | C++에서 임시로 1초마다 스폰 → 풀 개수가 늘지 않고 재사용되는지 |
| 3 | `SpawnEffectAttached` + 대상 파괴 처리 | 붙인 채 대상 `DestroyActor` → 크래시 없이 연기가 그 자리에서 사라지는지 |
| 4 | `SpawnTextEffect` | 카메라를 돌려도 글자가 대상 위에 붙어 있는지, 카메라 뒤에서 안 보이는지 |
| 5 | Lua 바인딩 + 게임 연결 | 실제 게임에서 세 가지 이펙트 |
| 6 | `Shutdown` | PIE를 여러 번 켰다 꺼도 크래시·누수 없는지 |

## 코드로 확인한 사항

- [x] 다른 액터의 컴포넌트에 붙여도 트랜스폼이 따라간다: `MarkTransformDirty`가 `AttachChildren`에 전파되고, `GetWorldMatrix`가 `AttachParent`를 따라 계산한다.
- [x] `DestroyActor`는 다른 액터 소유의 붙은 컴포넌트를 떼어내지 않는다 → 5-2 (a) 처리가 필요하다.
- [x] `FHUDRenderer`는 알파 블렌딩을 한다 → `Color.A`로 페이드가 된다.
- [x] `SetTexture`는 처음 부를 때 머티리얼 복사본을 만든다 → 이펙트마다 텍스처가 달라도 된다.
- [x] `UAssetManager::LoadTexture`는 같은 경로를 `AssetMap`에 캐시한다 → `SetTexture(경로)`를 매번 불러도 다시 로드하지 않는다.
- [ ] `SceneRenderer`가 `SetHiddenInGame`을 검사하는지 (2-3, `bAlive`로 처리하면 몰라도 됨)
