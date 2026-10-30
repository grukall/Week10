workspace "Hitori"
	architecture "x64"
	startproject "HitoriEditor"

	configurations
	{
		"Debug",
		"Release",
		"ObjViewer",
		"Benchmark",
		"Benchmark_Debug",
	}

	multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["ImGui"] = "Source/ThirdParty/ImGui"
IncludeDir["stb"]   = "Source/ThirdParty/stb"
IncludeDir["json"]  = "Source/ThirdParty/json"
IncludeDir["miniaudio"] = "Source/ThirdParty/miniaudio"
-- LuaJIT은 빌드 중에 CPU별 어셈블리 인터프리터를 생성해야 해서 premake로 소스를 빌드하지 않는다.
-- src/msvcbuild.bat(mixed, debug mixed)로 만든 정적 lib과 헤더를 저장소에 넣어 둔다.
IncludeDir["Lua"]	= "Source/ThirdParty/LuaJIT/include"
IncludeDir["sol2"]	= "Source/ThirdParty/sol2/include"
LuaJITLibDir = "Source/ThirdParty/LuaJIT/lib"

-- premake의 filter는 project()를 만나면 초기화된다.
-- 두 프로젝트가 같은 런타임(/MDd vs /MD)으로 컴파일되지 않으면 링크가 실패하므로
-- 공통 설정을 함수로 묶어 각 프로젝트에서 호출한다.
function CommonSettings()
	language   "C++"
	cppdialect "C++20"
	staticruntime "off"
	characterset  "Unicode"

	targetdir ("Build/Bin/" .. outputdir)
	objdir    ("Build/Intermediate/" .. outputdir)
	-- 에셋·쉐이더를 상대 경로로 읽으므로 작업 디렉터리는 저장소 루트다.
	debugdir  "%{wks.location}"

	defines
	{
		"WIN32_LEAN_AND_MEAN",
		"NOMINMAX",
		"UNICODE",
		"_UNICODE",
	}

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "system:windows"
		systemversion "latest"
		buildoptions { "/utf-8" }
		defines { "ENGINE_PLATFORM_WINDOWS" }

	filter "configurations:Debug"
		defines { "ENGINE_DEBUG", "_DEBUG" }
		runtime  "Debug"
		symbols  "on"

	filter "configurations:Release"
		defines  { "ENGINE_RELEASE", "NDEBUG" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	-- 에디터 없이 OBJ 파일만 열어보는 Viewer 빌드
	filter "configurations:ObjViewer"
		defines  { "ENGINE_RELEASE", "NDEBUG", "OBJ_VIEWER" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	filter "configurations:Benchmark_Debug"
		defines  { "ENGINE_DEBUG", "_DEBUG", "BENCHMARK" }
		runtime  "Debug"
		symbols  "on"

	filter "configurations:Benchmark"
		defines  { "ENGINE_RELEASE", "NDEBUG", "BENCHMARK" }
		runtime  "Release"
		optimize "speed"
		-- /GL + /LTCG는 끈다. 켠 exe가 Defender ML 탐지(Trojan:Win32/Wacatac)에 오탐으로 걸렸다.
		vectorextensions "AVX2"
		-- /GS-(버퍼 보안 검사 끄기)는 성능 이득이 거의 없고 백신 휴리스틱 오탐을 부르기 쉬워 켜 둔다 (기본값 /GS)
		buildoptions { "/Gw" }
		symbols  "on"



	filter {}
end

-- 실행 파일(Editor, Game)에서 호출한다. 엔진과 같은 C 런타임(/MDd, /MD)으로 빌드한 lib을 고른다.
function LinkLuaJIT()
	filter "configurations:Debug or Benchmark_Debug"
		libdirs { LuaJITLibDir .. "/Debug" }

	filter "configurations:Release or ObjViewer or Benchmark"
		libdirs { LuaJITLibDir .. "/Release" }

	filter {}

	links { "lua51" }
end


-- 외부 라이브러리. 별도 프로젝트로 두면 에디터의 소스 루트가 Source/Editor 하나로 좁혀져
-- Solution Explorer의 "모든 파일 표시"에서 폴더 구조가 그대로 보인다.
project "ImGui"
	location "Source/ThirdParty/ImGui"
	kind     "StaticLib"
	CommonSettings()

	files
	{
		"%{IncludeDir.ImGui}/*.h",
		"%{IncludeDir.ImGui}/*.cpp",
		"%{IncludeDir.ImGui}/backends/imgui_impl_win32.*",
		"%{IncludeDir.ImGui}/backends/imgui_impl_dx11.*",
	}

	includedirs
	{
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}

-- Lua 스크립트를 Solution Explorer에서 열어 보기 위한 프로젝트. 컴파일하지 않는다(Utility).
-- location을 스크립트 폴더로 두어 "모든 파일 표시"에서 새로 만든 .lua도 바로 보이게 한다.
project "Scripts"
	location "Assets/Scripts"
	kind     "Utility"
	CommonSettings()

	files
	{
		"Assets/Scripts/**.lua",
	}


-- 런타임. 에디터를 모른다. include 경로에 Source/Editor가 없는 것이 그 방벽이다.
-- location이 소스 루트와 같아야 "모든 파일 표시"에서 폴더가 보인다.
project "HitoriEngine"
	location "Source/Runtime"
	kind     "StaticLib"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Runtime/EnginePCH.cpp"

	files
	{
		"Source/Runtime/**.h",
		"Source/Runtime/**.hpp",
		"Source/Runtime/**.cpp",
	}

	includedirs
	{
		"Source/Runtime",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.Lua}",
		"%{IncludeDir.sol2}"
	}
	includedirs { "%{IncludeDir.miniaudio}" }
	files { "%{IncludeDir.miniaudio}/miniaudio.h", "%{IncludeDir.miniaudio}/LICENSE" }
	filter "files:Source/Runtime/Audio/Miniaudio.cpp"
		enablepch "Off"
	filter {}

	-- DirectX 라이브러리는 실행 파일 쪽(Editor, Game)에서 링크한다.
	-- 정적 라이브러리에 links를 주면 import lib가 HitoriEngine.lib 안으로 합쳐지고,
	-- Game의 /WHOLEARCHIVE가 그 멤버를 전부 끌어와 __NULL_IMPORT_DESCRIPTOR가 중복 정의된다.

	-- SOL_LUAJIT: sol2가 Lua 5.1 API(LuaJIT)로 컴파일되고 jit 라이브러리를 열 수 있게 한다
	defines
	{
		"SOL_ALL_SAFETIES_ON=1",
		-- LuaJIT 2.1에서 sol2는 기본으로 C++ 예외를 LuaJIT에 그대로 넘기는데,
		-- 그러면 메시지가 "C++ exception"으로 바뀐다. sol2가 직접 잡아 e.what()을 Lua 에러로 만들게 한다.
		"SOL_EXCEPTIONS_SAFE_PROPAGATION=0",
		"SOL_LUAJIT=1",
	}

	buildoptions
	{
		"/bigobj"
	}

-- 에디터 애플리케이션. ObjViewer 구성에서는 같은 exe가 뷰어로 빌드된다.
project "HitoriEditor"
	location "Source/Editor"
	kind     "WindowedApp"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Editor/EditorPCH.cpp"

	links { "HitoriEngine", "ImGui" }

	files
	{
		"Source/Editor/**.h",
		"Source/Editor/**.cpp",
		"Source/Programs/**.h",
		"Source/Programs/**.cpp",
		"Source/Editor/**.rc",   -- 창·exe 아이콘
	}

	includedirs
	{
		"Source",             -- "Editor/OutputLog/ConsolePanel.h"처럼 Source 기준 include를 쓴다
		"Source/Runtime",
		"Source/Programs",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.Lua}",
		"%{IncludeDir.sol2}"
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
		"HitoriEngine",
		"ImGui",
	}
	LinkLuaJIT()

	defines
	{
		"SOL_ALL_SAFETIES_ON=1",
		-- LuaJIT 2.1에서 sol2는 기본으로 C++ 예외를 LuaJIT에 그대로 넘기는데,
		-- 그러면 메시지가 "C++ exception"으로 바뀐다. sol2가 직접 잡아 e.what()을 Lua 에러로 만들게 한다.
		"SOL_EXCEPTIONS_SAFE_PROPAGATION=0",
		"SOL_LUAJIT=1",
	}

	buildoptions 
	{
		"/bigobj" 
	}

	filter "configurations:ObjViewer"
		targetname "ObjViewer"

	filter "configurations:Benchmark"
		targetname "Benchmark"

	filter "configurations:Benchmark_Debug"
		targetname "Benchmark_Debug"

	filter {}


-- 독립 실행 게임. 런타임만 링크하므로 에디터·ImGui 코드가 exe에 들어가지 않는다.
project "HitoriGame"
	location "Source/Game"
	kind     "WindowedApp"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Game/GamePCH.cpp"

	links { "HitoriEngine" }
	-- 클래스는 각 obj의 전역 변수(FAutoRegister)가 시작 시 등록하고, 씬 로드는 이름으로 찾는다.
	-- 게임 코드가 직접 참조하지 않는 클래스의 obj도 빠지지 않도록 엔진 lib를 통째로 링크한다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine.lib" }

	files
	{
		"Source/Game/**.h",
		"Source/Game/**.cpp",
	}

	includedirs
	{
		"Source/Runtime",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.Lua}",
		"%{IncludeDir.sol2}",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}
	LinkLuaJIT()

	-- sol2 설정은 엔진·에디터와 같아야 한다. 다르면 같은 sol2 템플릿이 프로젝트마다 다르게 컴파일된다.
	defines
	{
		"SOL_ALL_SAFETIES_ON=1",
		-- LuaJIT 2.1에서 sol2는 기본으로 C++ 예외를 LuaJIT에 그대로 넘기는데,
		-- 그러면 메시지가 "C++ exception"으로 바뀐다. sol2가 직접 잡아 e.what()을 Lua 에러로 만들게 한다.
		"SOL_EXCEPTIONS_SAFE_PROPAGATION=0",
		"SOL_LUAJIT=1",
	}

	buildoptions
	{
		"/bigobj",
	}
