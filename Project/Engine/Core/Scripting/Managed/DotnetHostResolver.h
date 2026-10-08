#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>

namespace Engine {

	//============================================================================
	//	DotnetHostResolver class
	//	hostfxrとManaged呼出入口を所有する
	//============================================================================
	class DotnetHostResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DotnetHostResolver() = default;
		~DotnetHostResolver();

		// DLLの二重解放を防ぐためコピーを禁止
		DotnetHostResolver(const DotnetHostResolver&) = delete;
		DotnetHostResolver& operator=(const DotnetHostResolver&) = delete;

		// Managed呼出入口を初期化
		bool Initialize(const std::filesystem::path& scriptCoreAssemblyPath, const std::filesystem::path& runtimeConfigPath);

		// 呼出入口を無効化してDLLを解放
		void Shutdown();

		//--------- accessor -----------------------------------------------------

		// DLL保持中のみ有効なAssembly読込入口
		void* GetLoadAssemblyDelegate() const { return loadAssemblyDelegate_; }
		// DLLと呼出入口を保持しているか
		bool IsInitialized() const { return library_ != nullptr && loadAssemblyDelegate_ != nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 所有するhostfxrのDLLハンドル
		void* library_ = nullptr;
		// ManagedAssemblyの読込入口
		void* loadAssemblyDelegate_ = nullptr;
	};
}
