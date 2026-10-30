-- BGM·효과음(SFX) 음량. 일시정지 창 슬라이더가 바꾸고, 엔진 사운드 매니저(Audio)에 반영한다.
--   local Sound = require("Assets.Scripts.Lib.Sound")
-- require는 처음 한 번만 실행하므로 씬이 바뀌어도 값이 유지된다.
-- (require는 파일을 다시 읽지 않으므로 이 파일을 고치면 에디터를 다시 켜야 반영된다)
--
-- 슬라이더 값(0~1)을 엔진 그룹 음량에 그대로 반영한다. 1이면 100%, 0이면 무음.

local Sound = {
	Volumes = { BGM = Audio.GetBGMVolume(), SFX = Audio.GetSFXVolume() },   -- 현재 그룹 음량
	Setters = { BGM = Audio.SetBGMVolume, SFX = Audio.SetSFXVolume },
}

-- Channel: "BGM" | "SFX"
function Sound.GetVolume(Channel)
	return Sound.Volumes[Channel]
end

function Sound.SetVolume(Channel, Value)
	Value = math.max(0, math.min(1, Value))
	Sound.Volumes[Channel] = Value
	Sound.Setters[Channel](Value)
end

return Sound
