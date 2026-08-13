#pragma once

//============================================================================
//	include
//============================================================================
#include <d3d12.h>

// c++
#include <array>
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	GraphicsFeatureTypes structures
	//============================================================================

	// GPUアダプタの基本情報
	struct GraphicsAdapterInfo {

		std::string adapterName{};
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		uint64_t dedicatedVideoMemoryBytes = 0;
	};


	// GPU/Driverが対応している機能を保持
	struct GraphicsFeatureSupport {

		// 一番ハイレベルのモデル
		D3D_SHADER_MODEL highestShaderModel = D3D_SHADER_MODEL_6_0;
		// メッシュシェーダのTier
		D3D12_MESH_SHADER_TIER meshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
		// レイトレーシングのTier
		D3D12_RAYTRACING_TIER raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
		bool waveOps = false;

		// メッシュシェーダ対応しているか
		bool SupportsShaderModel6_6() const { return D3D_SHADER_MODEL_6_6 <= highestShaderModel; }
		bool SupportsMeshShaderPath() const { return SupportsShaderModel6_6() && meshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
		// レイトレーシング対応しているか
		bool SupportsRayTracingPath() const { return D3D12_RAYTRACING_TIER_1_0 <= raytracingTier; }
		bool SupportsRayTracingTier1_1() const { return D3D12_RAYTRACING_TIER_1_1 <= raytracingTier; }
	};

	// メッシュLODの切り替え閾値
	namespace GraphicsMeshLOD {

		inline constexpr std::array<float, 3> kDefaultPixelThresholds = {
			160.0f, 80.0f, 32.0f
		};
		inline constexpr float kMinimumPixelThreshold = 1.0f;
		inline constexpr float kPixelThresholdGap = 4.0f;
		inline constexpr float kMaximumPixelThreshold = 4096.0f;

		bool ArePixelThresholdsValid(
			float lod0, float lod1, float lod2);
		std::array<float, 3> ClampPixelThresholds(
			float lod0, float lod1, float lod2);
	}

	// ユーザー参照設定
	enum class DisplayOutputMode :
		uint8_t {

		SDR,
		HDR10,
		ScRGB,
	};

	// SwapChainと最終出力変換で共有する表示設定
	struct DisplayOutputSettings {

		DisplayOutputMode mode = DisplayOutputMode::SDR;
		float paperWhiteNits = 200.0f;
		float maxLuminanceNits = 1000.0f;
	};

	// ユーザー参照設定
	struct GraphicsFeaturePreferences {

		// GPU対応状況とは別に、ユーザーが描画経路を許可するか
		bool allowMeshShader = true;
		bool allowInlineRayTracing = true;
		bool allowDispatchRays = false;
		// フラスタムカリングを行うか
		bool allowFrustumCulling = true;
		// 深度ピラミッドによるオクルージョンカリングを行うか
		bool allowOcclusionCulling = true;
		// SceneViewのカリングにGameViewのカメラを使用するか
		bool useGameViewCameraForSceneCulling = true;
		// 画面上の寄与が小さいメッシュ/メッシュレットを省くか
		bool allowContributionCulling = true;
		// MeshShader経路でメッシュレットの法線コーン判定を行うか
		bool allowNormalConeCulling = false;
		// メッシュLODを使用するか
		bool allowMeshLOD = true;
		// 投影半径が閾値を下回ったとき次のLODへ移る
		float meshLOD0PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[0];
		float meshLOD1PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[1];
		float meshLOD2PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[2];
		// 起動時に使用するフレームコンテキスト数
		uint32_t frameContextCount = 3;
		// 製品ランタイムのDisplay出力設定、エディターUIはSDR固定
		DisplayOutputSettings displayOutput{};
	};

	// ランタイムで使用する機能
	struct GraphicsRuntimeFeatures {

		// 対応状況とユーザー設定を解決した最終的な描画経路
		bool useMeshShader = false;
		bool useInlineRayTracing = false;
		bool useDispatchRays = false;
		// 描画パスごとに参照するカリング機能
		bool useFrustumCulling = false;
		bool useOcclusionCulling = false;
		bool useContributionCulling = false;
		bool useNormalConeCulling = false;
		bool useMeshLOD = true;
		float meshLOD0PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[0];
		float meshLOD1PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[1];
		float meshLOD2PixelThreshold =
			GraphicsMeshLOD::kDefaultPixelThresholds[2];

		bool UsesAnyRayTracing() const { return useInlineRayTracing || useDispatchRays; }
	};

	// エディター汎用関数
	namespace GraphicsFeatureText {

		const char* ToString(D3D_FEATURE_LEVEL value);
		const char* ToString(D3D_SHADER_MODEL value);
		const char* ToString(D3D12_MESH_SHADER_TIER value);
		const char* ToString(D3D12_RAYTRACING_TIER value);
	}
} // Engine
