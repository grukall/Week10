-- 게임 시작 연출: Assets/Sequences/Intro.sequence를 재생하고, 카메라가 게임 시점으로 돌아오면
-- GameState.FinishIntro로 알려서 GameHUD의 3, 2, 1 카운트다운이 시작된다.
--
-- 연출은 모두 시퀀스에 들어 있다 (무작위 값도 한 번 뽑아 고정해 두었다).
--   IntroCamera 바인딩  : 시네 카메라. 시작 앵글에서 화산을 비추고, 펑 할 때 시선을 올린다.
--                         3.4초(CameraCutEnd)에 3초 동안 EaseInOut으로 게임 카메라에 돌아간다.
--                         흔들림은 Event 트랙이 ACameraActor::StartCameraShake를 리플렉션으로 부른다.
--   Volcanoes 바인딩    : 이 액터 (BindingTag = "IntroVolcano"). 두 화산 메시의 우르릉·펑 크기 변화.
--   Piece_B01~C50       : 펑 할 때 꼭대기에서 터져 나오는 브로콜리·팝콘. Spawn 트랙으로 2.4초에 나타나 4초 뒤 사라진다.
--   마스터 Event 트랙   : 소리(PlaySound, StopSound)와 폭발 이펙트(SpawnEffect). 이펙트 프리셋도 시퀀스의 Effects에 있다.
--
-- 시간표 (초): 0 시작 앵글 → 1.0 우르릉 (0.35초마다 흔들림·작은 불꽃) → 2.4 펑 → 3.0 화산 원래 크기
--              → 3.4 게임 카메라로 전환 시작 → 6.4 전환 끝, 조각 사라짐, 카운트다운 시작

local GameState = require("Assets.Scripts.Lib.GameState")

local IntroSequence = "Assets/Sequences/Intro.sequence"

local Sequence = nil          -- 재생 중인 시퀀스 액터. 끝나면 스스로 지워진다

function BeginPlay()
    if not GetMainCamera() then
        GameState.FinishIntro() -- 연출을 못 하면 바로 카운트다운으로 넘어간다
        return
    end

    Sequence = PlaySequence(IntroSequence)
    if not Sequence then
        GameState.FinishIntro()
        return
    end

    start_coroutine(function()
        -- 시퀀스가 끝나 시퀀스 액터가 지워지고, 게임 카메라로의 블렌드도 끝나면 카운트다운을 시작한다
        wait_until(function() return not IsValid(Sequence) end)
        wait_until(function() return not IsViewTargetBlending() end)
        Sequence = nil
        GameState.FinishIntro()
    end)
end

function EndPlay()
    if IsValid(Sequence) then
        Sequence:Stop()
    end
end
