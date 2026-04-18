-------------------------------------------------------------------------------------------
-- os
-------------------------------------------------------------------------------------------
-- 作業ディレクトリをProjectフォルダに変更
os.chdir(_SCRIPT_DIR .. "/../Project")

-------------------------------------------------------------------------------------------
-- main solution
-------------------------------------------------------------------------------------------
solution "NEMEngine"

	-- 構成プロパティの定義
	configurations { "Debug", "Develop", "Release" }

	-- プラットフォームの設定
	platforms { "x64" }

	-- スタートプロジェクトの設定
	startproject "NEMEngine"

	-- 出力先の設定
	objdir "%{wks.location}/../Generated/obj/%{prj.name}/%{cfg.buildcfg}/" -- 中間ファイル

	-- ConsoleまたはWindowsApplicationの場合
	filter "kind:ConsoleApp or kind:WindowedApp"
		targetdir "%{wks.location}/../Generated/outputs/%{cfg.buildcfg}/"

	-- dll, libの場合
	filter "kind:StaticLib or kind:SharedLib"
    	targetdir "%{wks.location}/../Generated/bin/%{prj.name}/%{cfg.buildcfg}/" -- 出力先(StaticLibの場合)

	--- 外部projectの登録 ---
	-- [DirectXTex](https://github.com/microsoft/DirectXTex.git)
	externalproject "DirectXTex"
		location "Externals/DirectXTex"
		filename "DirectXTex_Desktop_2022_Win10"
		kind "StaticLib"
		language "C++"

		configmap {
        	["Develop"] = "Release",
 		}

-------------------------------------------------------------------------------------------
-- [imgui](https://github.com/ocornut/imgui.git) project
-------------------------------------------------------------------------------------------
project "imgui"

	-- 構成プロパティの修正(DevelopをReleaseと同等に)
	removeconfigurations { "Develop" }
    configmap { ["Develop"] = "Release" }

	-- フォルダ指定
	location "Externals/imgui/Project"

	-- visual studioの設定
	toolset "v143"

	-- projectの種類
	kind "StaticLib"

	-- 言語
	language "c++"
	cppdialect "c++20"

	-- ファイルの追加
	files {
		"%{prj.location}/../*.cpp",
		"%{prj.location}/../*.h",
	}

	-- 追加include
	includedirs {
		"%{prj.location}/../",
	}

	-- ビルドオプション(共通)
	warnings "Default" -- 警告Level3
	multiprocessorcompile "On" -- 複数コアのでの並列コアコンパイル
	staticruntime "On"
	buildoptions { "/utf-8" }

	--- 構成ごとの設定 ---
	filter "configurations:Debug"
		-- ビルドオプション 
			symbols "On"
		
	filter "configurations:Release"
		-- ビルドオプション
		optimize "On"

-------------------------------------------------------------------------------------------
-- main c++ project
-------------------------------------------------------------------------------------------
project "NEMEngine"

	-- visual studioの設定
	toolset "v143"

	-- projectの種類
	kind "WindowedApp"

	-- 言語
	language "c++"
	cppdialect "c++20"

	-- ファイルの追加
	files {
		-- Engineフォルダ内全参照
		"%{prj.location}/Engine/**.cpp",
		"%{prj.location}/Engine/**.h",
		
		-- Gameフォルダ内全参照
		"%{prj.location}/Game/**.cpp",
		"%{prj.location}/Game/**.h",
	}

	-- 追加インクルード
	includedirs {
		"%{prj.location}",
		"%{prj.location}/Externals/spdlog", -- [spdlog](https://github.com/gabime/spdlog.git)
		"%{prj.location}/Externals/magic_enum", -- [magic_enum](https://github.com/Neargye/magic_enum.git)
		"%{prj.location}/externals/assimp/include", -- [assimp](https://github.com/assimp/assimp.git)
		"%{prj.location}/Externals/meshoptimizer/include", -- [meshoptimizer](https://github.com/zeux/meshoptimizer.git)
		"%{prj.location}/Externals/DirectXTex", -- [DirectXTex](https://github.com/microsoft/DirectXTex.git)
		"%{prj.location}/Externals/imgui", -- [imgui](https://github.com/ocornut/imgui.git)
	}

	-- 依存プロジェクト --
	dependson {
		"DirectXTex",
		"imgui"
	}

	links {
		"DirectXTex",
		"imgui"
	}

	-- ビルドオプション(共通) --
	warnings "High"
	multiprocessorcompile "On" -- 複数コアのでの並列コアコンパイル
	staticruntime "On"
	buildoptions { "/utf-8" }

	-- define定義(共通) --
	defines {
		'_PROFILE="$(Configuration)"',
		"NOMINMAX",
		"IMGUI_DEFINE_MATH_OPERATORS",
	}

	-- リンカー設定(共通) --
	linkoptions {
		"/WX",
		"/IGNORE:4099", -- [LNK4099](https://learn.microsoft.com/ja-jp/cpp/error-messages/tool-errors/linker-tools-warning-lnk4099)
	}

	-- ビルド後イベント --
	postbuildcommands {
		-- dxcompiler関係
		'copy "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"',
  		'copy "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxil.dll" "$(TargetDir)dxil.dll"',
	}

	--- 外部プログラムごとの設定 ---
	-- Assimp
	filter "configurations:Debug"
		-- リンカー設定
		libdirs {
			"%{prj.location}/Externals/Assimp/lib/Debug"
		}

		-- 依存ファイル
		links {
			"assimp-vc143-mtd",
		}

	filter "configurations:Develop or configurations:Release"
		-- リンカー設定 --
		libdirs {
			"%{prj.location}/Externals/Assimp/lib/Release"
		}

		-- 依存ファイル --
		links {
			"assimp-vc143-mt",
		}

	-- meshoptimizer
	filter "configurations:Debug"
		-- リンカー設定
		libdirs {
			"%{prj.location}/Externals/meshoptimizer/lib/Debug"
		}

		-- 依存ファイル
		links {
			"meshoptimizer"
		}

	filter "configurations:Develop or configurations:Release"
		-- リンカー設定 --
		libdirs {
			"%{prj.location}/Externals/meshoptimizer/lib/Release"
		}

		-- 依存ファイル --
		links {
			"meshoptimizer",
		}

	--- project構成ごとのビルドオプション設定 ---
	-- Debug
	filter "configurations:Debug"
		-- ビルドオプション
		symbols "On"
		
		-- define定義
		defines { "_DEVELOPBUILD" }

	-- Develop
	filter "configurations:Develop"
		-- ビルドオプション
		optimize "On"
		
		-- define定義
		defines { "_DEVELOPBUILD" }

	-- Release
	filter "configurations:Release"
		-- ビルドオプション
		optimize "On"

		buildoptions {
			"/wd4100" -- [C4100](https://learn.microsoft.com/ja-jp/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4100)
		}

		-- define定義
		defines { "_RELEASE" }