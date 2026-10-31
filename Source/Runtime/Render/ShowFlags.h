#pragma once
struct FEngineShowFlags
{
    bool Fog = true;
    bool AntiAliasing = true;       // FXAA
    bool Tonemapper = true;
    bool GammaCorrection = true;
    bool Vignette = true;
    bool PostProcessing = true;     // 후처리 전체 끔
    bool Decal = true;
};

// 데칼을 DBuffer에 그리는 방식. 둘의 결과와 비용을 비교하려고 고를 수 있게 둔다.
// Depth: 데칼 박스를 그리고 깊이 텍스처로 픽셀의 월드 위치를 복원한다.
// Mesh: 데칼을 받는 메시를 다시 그려 보간된 월드 위치를 쓴다.
enum class EDecalMethod { Depth, Mesh };
