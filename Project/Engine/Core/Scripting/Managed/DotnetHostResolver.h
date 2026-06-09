#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace Engine {

	//============================================================================
	//	DotnetHostResolver class
	//	nethostのget_hostfxr_pathでhostfxrを探索し、load_assembly_and_get_function_pointer
	//	デリゲートまで取得して保持するRAIIサービス。
	class DotnetHostResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		DotnetHostResolver() = default;
		~DotnetHostResolver();

		// 多重解放・二重復元を防ぐためコピー/ムーブ禁止
		DotnetHostResolver(const DotnetHostResolver&) = delete;
		DotnetHostResolver& operator=(const DotnetHostResolver&) = delete;

		// get_hostfxr_path → LoadLibrary → hostfxr_initialize_for_runtime_config →
		// hostfxr_get_runtime_delegate(hdt_load_assembly_and_get_function_pointer) までを実行する。
		// 失敗段階ごとに区別した診断ログを出し、いずれの失敗経路でも内部リソースを解放する。
		// 成功でtrue。多重呼び出し時は先に既存状態をShutdownしてから再初期化する。
		bool Initialize(const std::filesystem::path& scriptCoreAssemblyPath,
			const std::filesystem::path& runtimeConfigPath);

		// hostfxrライブラリを解放し、デリゲートを無効化する。複数回呼び出しても安全。
		void Shutdown();

		//--------- accessor -----------------------------------------------------

		// load_assembly_and_get_function_pointer デリゲート。
		// 戻り値はhostfxrライブラリがロード中のみ有効。呼び出し側で実シグネチャへcastする。
		void* GetLoadAssemblyDelegate() const { return loadAssemblyDelegate_; }
		bool IsInitialized() const { return library_ != nullptr && loadAssemblyDelegate_ != nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// hostfxr.dll の HMODULE（windows.h非公開化のためvoid*で保持）
		void* library_ = nullptr;
		// load_assembly_and_get_function_pointer デリゲート
		void* loadAssemblyDelegate_ = nullptr;
	};
} // Engine
