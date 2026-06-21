#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace Engine {

	//============================================================================
	//	DotnetHostResolver class
	//	nethostでhostfxrを探索しload_assembly_and_get_function_pointerデリゲートまで取得して保持するRAIIサービス
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

		// hostfxr初期化からload_assembly_and_get_function_pointer取得までを実行し失敗段階ごとに診断ログを出してリソースを解放する、成功でtrueで多重呼び出し時は先にShutdownしてから再初期化する
		bool Initialize(const std::filesystem::path& scriptCoreAssemblyPath,
			const std::filesystem::path& runtimeConfigPath);

		// hostfxrライブラリを解放しデリゲートを無効化する、複数回呼び出しても安全
		void Shutdown();

		//--------- accessor -----------------------------------------------------

		// load_assembly_and_get_function_pointerデリゲート、戻り値はhostfxrロード中のみ有効で呼び出し側が実シグネチャへcastする
		void* GetLoadAssemblyDelegate() const { return loadAssemblyDelegate_; }
		bool IsInitialized() const { return library_ != nullptr && loadAssemblyDelegate_ != nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// hostfxr.dllのHMODULE、windows.h非公開化のためvoid*で保持する
		void* library_ = nullptr;
		// load_assembly_and_get_function_pointerデリゲート
		void* loadAssemblyDelegate_ = nullptr;
	};
} // Engine
