#pragma once

#include "Core/EngineString.h"

// 에디터에서 정하고 빌드(게임 exe)가 그대로 따르는 프로젝트 공용 설정.
// editor.ini(개인 작업 설정)와 달리 저장소에 올리고 패키징에 포함한다.
struct FProjectSettings
{
	// 작업 디렉터리 기준 상대 경로. 패키징 폴더에서도 찾을 수 있도록 절대 경로를 쓰지 않는다.
	static constexpr const char* DefaultPath = "Config/Project.json";

	// 작업 디렉터리 기준 UTF-8 상대 경로. 비어 있으면 빈 월드로 시작한다.
	FString StartScene;

	// 파일이 없거나 형식이 잘못됐으면 경고를 남기고 false를 반환한다. 이때 값은 기본값 그대로다.
	bool Load(const FString& Path);
	// 폴더가 없으면 만든다. 기존 파일의 모르는 키는 보존한다.
	bool Save(const FString& Path) const;

	// UTF-8로 저장된 경로를 파일 API가 받는 시스템 코드페이지 문자열로 바꾼다. 한글 경로용이다.
	static FString ToNativePath(const FString& Utf8Path);
};
