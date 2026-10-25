#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>

// c++
#include <chrono>
#include <cstdint>

// directX
#include <dxgi1_6.h>

namespace Engine {

	// 表示先で利用できない出力の理由
	enum class DisplayOutputFallback : uint8_t {

		None,
		HDRUnavailable,
		ColorSpaceUnsupported,
		ReconfigurationFailed,
	};

	// 出力画像と描画先の形式
	struct DisplayOutputFormat {

		DXGI_FORMAT buffer = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT target = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	};

	//============================================================================
	//	DisplayOutputState class
	// モニターの表示能力と希望設定から実際の出力を決める
	//============================================================================
	class DisplayOutputState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 表示先と希望設定を更新する
		bool Refresh(HWND hwnd, IDXGIFactory* factory, const DisplayOutputSettings& requested, bool force = false);
		// 利用できない出力をSDRへ制限する
		void UseSDR(DisplayOutputFallback reason);
		// 希望設定を保持して有効だった出力へ戻す
		void RestoreOutput(const DisplayOutputState& previous);
		// 色空間とHDRメタデータを適用する
		bool Apply(IDXGISwapChain3* swapChain) const;
		// 表示形式を解決する
		static DisplayOutputFormat ResolveFormat(DisplayOutputMode mode, DXGI_FORMAT sdrFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
		// 表示能力に応じて希望設定を制限する
		static DisplayOutputSettings ResolveSettings(const DisplayOutputSettings& requested, bool hdrEnabled);

		//--------- accessor -----------------------------------------------------

		const DisplayOutputSettings& GetSettings() const { return settings_; }
		DisplayOutputFallback GetFallback() const { return fallback_; }
		float GetUIWhiteNits() const { return uiWhiteNits_; }
		uint64_t GetRevision() const { return revision_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		DisplayOutputSettings requested_{};
		DisplayOutputSettings settings_{};
		DisplayOutputFallback fallback_ = DisplayOutputFallback::None;
		HMONITOR monitor_ = nullptr;
		float uiWhiteNits_ = 200.0f;
		bool hdrEnabled_ = false;
		uint64_t revision_ = 0;
		std::chrono::steady_clock::time_point nextRefresh_{};

		//--------- functions ----------------------------------------------------

		// WindowsのSDR基準白を取得する
		static float QueryUIWhiteNits(HMONITOR monitor);
		// モニターのHDR有効状態を取得する
		static bool QueryHDREnabled(IDXGIFactory* factory, HMONITOR monitor);
	};
} // Engine
