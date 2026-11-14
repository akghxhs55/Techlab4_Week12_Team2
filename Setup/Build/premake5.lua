local ProjectRoot = path.getabsolute("../..", _SCRIPT_DIR)

-- ImGui는 다른 외부 라이브러리처럼 소스에 포함한다.
if _ACTION and not os.isfile(ProjectRoot .. "/Source/ThirdParty/ImGui/imgui.h") then
	error("Bundled ImGui sources are missing. Restore Source/ThirdParty/ImGui from this repository.")
end

workspace "Hitori"
	location (ProjectRoot)
	architecture "x64"
	startproject "HitoriEditor"

	configurations
	{
		"Debug",
		"Release",
		"Shipping",
	}


	multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- 에디터 FBX 임포터는 저장소에 포함한 Windows x64 동적 링크 SDK를 사용한다.
local FbxSdkRoot = path.join(ProjectRoot, "Source/ThirdParty/FBX")
if _ACTION then
	for _, File in ipairs({ "include/fbxsdk.h", "lib/x64/debug/libfbxsdk.lib", "lib/x64/debug/libfbxsdk.dll", "lib/x64/release/libfbxsdk.lib", "lib/x64/release/libfbxsdk.dll" }) do
		if not os.isfile(path.join(FbxSdkRoot, File)) then
			error("Bundled FBX SDK file is missing: " .. File)
		end
	end
end

IncludeDir = {}
IncludeDir["ImGui"] = ProjectRoot .. "/Source/ThirdParty/ImGui"
IncludeDir["ImGuiNodeEditor"] = ProjectRoot .. "/Source/ThirdParty/ImGuiNodeEditor"
IncludeDir["stb"]   = ProjectRoot .. "/Source/ThirdParty/stb"
IncludeDir["json"]  = ProjectRoot .. "/Source/ThirdParty/json"
IncludeDir["DDSLoader"] = ProjectRoot .. "/Source/ThirdParty/DDSLoader"
IncludeDir["lua"]   = ProjectRoot .. "/Source/ThirdParty/lua/src"
IncludeDir["sol2"]  = ProjectRoot .. "/Source/ThirdParty/sol2/include"
IncludeDir["miniaudio"]  = ProjectRoot .. "/Source/ThirdParty/miniaudio"

-- premake의 filter는 project()를 만나면 초기화된다.
-- 두 프로젝트가 같은 런타임(/MDd vs /MD)으로 컴파일되지 않으면 링크가 실패하므로
-- 공통 설정을 함수로 묶어 각 프로젝트에서 호출한다.
function CommonSettings()
	language   "C++"
	cppdialect "C++20"
	staticruntime "off"
	characterset  "Unicode"

	targetdir (ProjectRoot .. "/Build/Bin/" .. outputdir)
	objdir    (ProjectRoot .. "/Build/Intermediate/" .. outputdir .. "/%{prj.name}")
	-- 에셋·쉐이더를 상대 경로로 읽으므로 작업 디렉터리는 저장소 루트다.
	debugdir (ProjectRoot)

	defines
	{
		"WIN32_LEAN_AND_MEAN",
		"NOMINMAX",
		"UNICODE",
		"_UNICODE",
		"SOL_ALL_SAFETIES_ON=1", 
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


	filter "configurations:Shipping"
		defines  { "ENGINE_RELEASE", "ENGINE_SHIPPING", "NDEBUG" }
		runtime  "Release"
		optimize "speed"
		symbols  "on"


	filter {}
end


-- 헤더 툴 (UE의 UnrealHeaderTool 역할). 엔진 코드에 의존하지 않고 std만 쓴다.
-- HitoriEngine / HitoriEditor가 빌드 전에 이 exe로 *.generated.h를 만든다.
HeaderTool = '"' .. ProjectRoot .. '/Build/Bin/' .. outputdir .. '/HitoriHeaderTool.exe"'
GeneratedDir = ProjectRoot .. "/Build/Generated"

project "HitoriHeaderTool"
	location (ProjectRoot .. "/Build/Projects")
	kind     "ConsoleApp"
	CommonSettings()

	files
	{
		ProjectRoot .. "/Source/Programs/HitoriHeaderTool/**.h",
		ProjectRoot .. "/Source/Programs/HitoriHeaderTool/**.cpp",
	}

-- 런타임. 에디터를 모른다. include 경로에 Source/Editor가 없는 것이 그 방벽이다.
project "HitoriEngine"
	location (ProjectRoot .. "/Build/Projects")
	kind     "StaticLib"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource (ProjectRoot .. "/Source/Runtime/EnginePCH.cpp")

	files
	{
		ProjectRoot .. "/Source/Runtime/**.h",
		ProjectRoot .. "/Source/Runtime/**.hpp",
		ProjectRoot .. "/Source/Runtime/**.cpp",
		"%{IncludeDir.DDSLoader}/*.h",
		"%{IncludeDir.DDSLoader}/*.cpp",
	}

	includedirs
	{
		ProjectRoot .. "/Source/Runtime",
		ProjectRoot .. "/Build/Generated/Runtime",   -- 추가
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.DDSLoader}",
		"%{IncludeDir.miniaudio}",
		"%{IncludeDir.lua}",
		"%{IncludeDir.sol2}",
	}

	-- Lua는 엔진에 포함한다. 인터프리터/컴파일러 실행 파일의 main은 제외한다.
	files { "%{IncludeDir.lua}/*.h", "%{IncludeDir.lua}/*.c" }
	removefiles { "%{IncludeDir.lua}/lua.c", "%{IncludeDir.lua}/luac.c" }
	vpaths { ["ThirdParty/Lua/*"] = "%{IncludeDir.lua}/*" }
	filter "files:**/lua/src/**.c"
		compileas "C"
		enablepch "Off"
	filter {}

	dependson { "HitoriHeaderTool" }
	prebuildcommands { HeaderTool .. ' Runtime "' .. GeneratedDir .. '/Runtime" "' .. ProjectRoot .. '/Source/Runtime"' }

	-- sol2 템플릿이 섹션 수 제한을 넘긴다
	filter "files:**/Script/**.cpp"
		buildoptions { "/bigobj" }
	filter {}

	-- d3d11·dxgi 등 시스템 라이브러리는 여기서 링크하지 않는다. exe 프로젝트들이 링크한다.
	-- 정적 라이브러리가 임포트 라이브러리를 품고 있으면 exe의 /WHOLEARCHIVE가 그것까지 끌어와
	-- __NULL_IMPORT_DESCRIPTOR 중복 정의(LNK2005)가 난다.

	filter "files:**/DDSLoader/**.cpp"
		enablepch "Off"
	filter {}

	filter "files:**/Audio/MiniaudioImpl.cpp"
		enablepch "Off"
	filter {}

-- 에디터 애플리케이션. 정적 메시와 스켈레탈 메시 뷰어는 에디터 내부에서 실행한다.
project "HitoriEditor"
	location (ProjectRoot .. "/Build/Projects")
	kind     "WindowedApp"
	CommonSettings()

	-- FBX 해석은 Editor에서 수행하고 Runtime/Game은 SDK에 의존하지 않는다.
	includedirs { path.join(FbxSdkRoot, "include"), ProjectRoot, ProjectRoot .. "/Source/Editor/Resources" }
	defines { "FBXSDK_SHARED" }
	links { "libfbxsdk.lib" }

	for _, Config in ipairs({ "Debug", "Release", "Shipping" }) do
		local SdkConfig = (Config == "Debug") and "debug" or "release"
		local SdkLibDir = path.join(FbxSdkRoot, "lib/x64", SdkConfig)
		filter ("configurations:" .. Config)
			libdirs { SdkLibDir }
			postbuildcommands { '{COPYFILE} "' .. path.join(SdkLibDir, "libfbxsdk.dll") .. '" "%{cfg.targetdir}"' }
	end
	filter {}

	pchheader "EnginePCH.h"
	pchsource (ProjectRoot .. "/Source/Editor/EditorPCH.cpp")

	links { "HitoriEngine" }

	-- ImGui와 NodeEditor는 에디터에서만 컴파일하고 엔진 PCH를 사용하지 않는다.
	files
	{
		"%{IncludeDir.ImGui}/*.h",
		"%{IncludeDir.ImGui}/*.cpp",
		"%{IncludeDir.ImGui}/backends/imgui_impl_win32.*",
		"%{IncludeDir.ImGui}/backends/imgui_impl_dx11.*",
		"%{IncludeDir.ImGuiNodeEditor}/*.h",
		"%{IncludeDir.ImGuiNodeEditor}/*.cpp",
		"%{IncludeDir.ImGuiNodeEditor}/*.inl",
	}
	vpaths
	{
		["ThirdParty/ImGui/*"] = "%{IncludeDir.ImGui}/**",
		["ThirdParty/ImGuiNodeEditor/*"] = "%{IncludeDir.ImGuiNodeEditor}/**",
	}
	filter "files:**/ThirdParty/**.cpp"
		enablepch "Off"
	filter {}

	-- UClass는 헤더의 정적 객체가 등록하므로, exe가 직접 참조하지 않는 클래스의 .obj를 링커가 버리면
	-- 그 클래스가 등록되지 않아 씬 로드 때 액터가 사라진다. 엔진 .obj를 전부 링크해 막는다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine" }

	files
	{
		ProjectRoot .. "/Source/Editor/**.h",
		ProjectRoot .. "/Source/Editor/**.cpp",
		ProjectRoot .. "/Source/Editor/**.rc",   -- 창·exe 아이콘
		-- 프로세스 ANSI 코드 페이지를 UTF-8로 둔다. 한글 프로젝트 경로가 char 경로 API에서 깨지지 않게 한다.
		ProjectRoot .. "/Source/Runtime/Launch/HitoriApp.manifest",
	}


	dependson { "HitoriHeaderTool" }
	prebuildcommands { HeaderTool .. ' Editor "' .. GeneratedDir .. '/Editor" "' .. ProjectRoot .. '/Source/Editor"' }

	includedirs
	{
		ProjectRoot .. "/Source",             -- "Editor/OutputLog/ConsolePanel.h"처럼 Source 기준 include를 쓴다
		ProjectRoot .. "/Source/Runtime",
		ProjectRoot .. "/Source/Programs",
		ProjectRoot .. "/Build/Generated/Runtime",   -- 추가: 엔진 헤더가 include하는 generated.h
		ProjectRoot .. "/Build/Generated/Editor",    -- 추가
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
		"%{IncludeDir.ImGuiNodeEditor}",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.lua}",
		"%{IncludeDir.sol2}",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}



	filter {}

-- 패키징용 게임 실행 파일. 에디터 소스와 ImGui를 링크하지 않는다.
-- 에셋과 셰이더는 실행 중에 읽으므로 배포 시 Package.bat으로 콘텐츠를 함께 복사한다.
project "HitoriGame"
	location (ProjectRoot .. "/Build/Projects")
	kind     "WindowedApp"
	CommonSettings()
	targetname "HitoriGame"

	pchheader "EnginePCH.h"
	pchsource (ProjectRoot .. "/Source/Game/GamePCH.cpp")

	links { "HitoriEngine" }

	-- 에디터와 같은 이유. 게임 코드는 라이트 액터 등을 직접 참조하지 않아 특히 빠지기 쉽다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine" }

	files
	{
		ProjectRoot .. "/Source/Game/**.h",
		ProjectRoot .. "/Source/Game/**.cpp",
		ProjectRoot .. "/Source/Game/**.rc",   -- 아이콘을 넣을 때
		ProjectRoot .. "/Source/Runtime/Launch/HitoriApp.manifest",   -- 에디터와 같은 이유 (UTF-8 코드 페이지)
	}

	includedirs
	{
		ProjectRoot .. "/Source",
		ProjectRoot .. "/Source/Runtime",
		ProjectRoot .. "/Build/Generated/Runtime",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.lua}",
		"%{IncludeDir.sol2}",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}


-- 셰이더를 VS에서 편집하기 위한 보기 전용 프로젝트 (언리얼의 Shaders와 같은 역할).
-- 에셋(Assets)은 에디터 Content Drawer에서 다루므로 솔루션에 넣지 않는다.
-- 새 셰이더 파일은 SlnGenerator.bat을 다시 돌려야 목록에 반영된다.
project "Shaders"
	location (ProjectRoot .. "/Build/Projects")
	kind     "Utility"

	files { ProjectRoot .. "/EngineResources/Shaders/**.hlsl" }
	files { ProjectRoot .. "/EngineResources/Shaders/**.hlsli" }
	-- VS의 논리 필터만 분류한다. 런타임에서 사용하는 실제 파일 경로는 유지한다.
	local ShaderRoot = ProjectRoot .. "/EngineResources/Shaders/"
	local ShaderGroups =
	{
		Common = { "*.hlsli" },
		Surface = { "DefaultShader.hlsl", "UberLit.hlsl", "StaticMeshTranslucentShader.hlsl", "DecalShader.hlsl", "SkyboxShader.hlsl" },
		Depth = { "DepthPrepass.hlsl", "SceneDepthShader.hlsl" },
		Shadows = { "Shadow*.hlsl", "CascadeFloorShader.hlsl" },
		Lighting = { "TileBasedLightCulling.hlsl", "LightCullingDebug.hlsl" },
		PostProcess = { "CameraOverlay.hlsl", "FxaaShader.hlsl", "HeightFogShader.hlsl", "OutlineShader.hlsl", "ToneMapShader.hlsl" },
		Effects = { "BillboardShader.hlsl" },
		Editor = { "BoundingBoxShader.hlsl", "GizmoShader.hlsl", "GridShader*.hlsl" },
		UI = { "TextShader.hlsl", "UIShader.hlsl" },
	}
	local ShaderFilters = {}
	local ClassifiedShaders = {}
	for Group, Patterns in pairs(ShaderGroups) do
		local Paths = {}
		for _, Pattern in ipairs(Patterns) do
			for _, File in ipairs(os.matchfiles(ShaderRoot .. Pattern)) do
				table.insert(Paths, File)
				ClassifiedShaders[File] = true
			end
		end
		ShaderFilters[Group .. "/*"] = Paths
	end
	-- 겹치는 와일드카드 대신 미분류 파일만 따로 매핑해 필터 우선순위에 의존하지 않는다.
	local Uncategorized = {}
	for _, Pattern in ipairs({ "**.hlsl", "**.hlsli" }) do
		for _, File in ipairs(os.matchfiles(ShaderRoot .. Pattern)) do
			if not ClassifiedShaders[File] then
				table.insert(Uncategorized, File)
			end
		end
	end
	ShaderFilters["Uncategorized/*"] = Uncategorized
	vpaths (ShaderFilters)

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "files:**.hlsli"
		excludefrombuild "On"

	filter {}


