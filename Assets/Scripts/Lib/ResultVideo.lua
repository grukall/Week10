-- 종료 판정과 점수는 GameState/GameHUD가 맡고, 이 모듈은 결과 영상만 재생한다.
local Video = {}
local ClipPaths = {
    [0] = "Assets/SubUV/Videos/Draw/clip.lua",
    [1] = "Assets/SubUV/Videos/Chikawa_5_11_clean/clip.lua",
    [2] = "Assets/SubUV/Videos/Usagi_0_10/clip.lua",
}

function Video.Reset()
    if Video.SoundId and Video.SoundId ~= 0 then Audio.Stop(Video.SoundId) end
    Video.Clip = nil
    Video.SoundId = nil
    Video.Elapsed = 0
end

function Video.Play(Winner)
    Video.Reset()
    Video.Clip = dofile(ClipPaths[Winner or 0])
    Audio.StopBGM()
    Video.SoundId = Audio.Play(Video.Clip.AudioPath)
end

function Video.IsPlaying()
    return Video.Clip ~= nil and Video.Elapsed < Video.Clip.Duration
end

function Video.Update(dt)
    if not Video.IsPlaying() then return end
    if Video.SoundId ~= 0 and Audio.IsPlaying(Video.SoundId) then
        Video.Elapsed = Audio.GetPosition(Video.SoundId)
    else
        -- 오디오 장치/파일이 없거나 소리가 먼저 끝나도 영상은 끝까지 진행한다.
        Video.Elapsed = Video.Elapsed + dt
    end
    Video.Elapsed = math.min(Video.Elapsed, Video.Clip.Duration)
    if not Video.IsPlaying() and Video.SoundId ~= 0 then
        Audio.Stop(Video.SoundId)
        Video.SoundId = 0
    end
end

function Video.GetVideoFrame()
    local Clip = Video.Clip
    if not Clip then return end
    local Frame = math.min(math.floor(Video.Elapsed * Clip.FrameRate), Clip.FrameCount - 1)
    for _, Page in ipairs(Clip.Pages) do
        if Frame >= Page.FirstFrame and Frame < Page.FirstFrame + Page.FrameCount then
            local LocalFrame = Frame - Page.FirstFrame
            local X = (LocalFrame % Clip.Columns) * Clip.CellWidth + Clip.Padding
            local Y = math.floor(LocalFrame / Clip.Columns) * Clip.CellHeight + Clip.Padding
            return Page.Texture, (X + 0.5) / Page.Width, (Y + 0.5) / Page.Height,
                (X + Clip.FrameWidth - 0.5) / Page.Width, (Y + Clip.FrameHeight - 0.5) / Page.Height
        end
    end
end

return Video
