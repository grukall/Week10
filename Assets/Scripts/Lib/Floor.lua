-- GameJamMap0 매장 바닥 범위 (월드 좌표). 플레이어 이동 제한과 낙하물 위치에 같이 쓴다.
-- 맵 메시(Scale 0.01)를 위에서 내려다본 이미지로 잰 값
local Floor = {
	MinX = -5.05, MaxX = 3.35,   -- 입구 쪽 ~ 카운터 앞
	MinY = -5.55, MaxY = 5.60,   -- 양쪽 벽
	Z = 0.0,                     -- 바닥 높이

	-- 플레이 영역은 바닥 가운데를 중심으로 한 원이다. 바닥이 X로 짧아서(반폭 4.2) 그 안에 들어가게 잡는다
	PlayerRadius = 4.4,          -- 플레이어 중심이 이 원 안에서만 움직인다
	DropRadius = 4.1,            -- 낙하물은 이 원 안에만 떨어진다 (플레이어가 닿을 수 있게 PlayerRadius보다 작게)
}

Floor.CenterX = (Floor.MinX + Floor.MaxX) * 0.5
Floor.CenterY = (Floor.MinY + Floor.MaxY) * 0.5

return Floor
