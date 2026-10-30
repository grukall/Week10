#pragma once
struct FEngineShowFlags
{
    bool Fog = true;
    bool AntiAliasing = true;       // FXAA
    bool Tonemapper = true;
    bool GammaCorrection = true;
    bool Vignette = true;           
    bool PostProcessing = true;     // 후처리 전체 끔
};