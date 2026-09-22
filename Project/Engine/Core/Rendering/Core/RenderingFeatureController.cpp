#include "RenderingFeatureController.h"

//============================================================================
//	include
//============================================================================
#include "GraphicsPreferenceStorage.h"
#include "GraphicsFeatureSelection.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	GraphicsFeatureController classMethods
//============================================================================
namespace {

	const char* GetEnabledText(bool enabled) {

		return enabled ? "有効" : "無効";
	}
}

void Engine::GraphicsFeatureController::ApplyDetectedSupport(
	const GraphicsAdapterInfo& adapterInfo, const GraphicsFeatureSupport& support) {

	// 初回適用かどうか
	const bool firstApply = !initialized_;

	adapterInfo_ = adapterInfo;
	support_ = support;

	// 初回適用時は、サポート状況を元にユーザー設定の初期値を決定する
	if (firstApply) {

		preferences_.allowInlineRayTracing = support_.SupportsRayTracingPath();
		preferences_.allowDispatchRays = support_.SupportsRayTracingPath();
		LoadPreferencesFromConfig();
		initialized_ = true;
	}
	ClampPreferencesToSupport();
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();
	LogCurrentState();
}

void Engine::GraphicsFeatureController::SetAllowMeshShader(bool enabled) {

	// サポートしていない機能を有効にしようとした場合は、強制的に無効にする
	bool clamped = enabled && support_.SupportsMeshShaderPath();
	if (preferences_.allowMeshShader == clamped) {
		return;
	}

	// 設定を更新して、ランタイムの機能も再構築する
	preferences_.allowMeshShader = clamped;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "Mesh Shader経路: {}", GetEnabledText(runtimeFeatures_.useMeshShader));
}

void Engine::GraphicsFeatureController::SetAllowInlineRayTracing(bool enabled) {

	bool clamped = enabled && support_.SupportsRayTracingPath();
	if (preferences_.allowInlineRayTracing == clamped) {
		return;
	}

	preferences_.allowInlineRayTracing = clamped;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "Inline RayTracing経路: {}", GetEnabledText(runtimeFeatures_.useInlineRayTracing));
}

void Engine::GraphicsFeatureController::SetAllowDispatchRays(bool enabled) {

	bool clamped = enabled && support_.SupportsRayTracingPath();
	if (preferences_.allowDispatchRays == clamped) {
		return;
	}

	preferences_.allowDispatchRays = clamped;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "DispatchRays経路: {}", GetEnabledText(runtimeFeatures_.useDispatchRays));
}

void Engine::GraphicsFeatureController::SetAllowRaytracingDownsampling(
	bool enabled) {

	if (preferences_.allowRaytracingDownsampling == enabled) {
		return;
	}

	preferences_.allowRaytracingDownsampling = enabled;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();
	Logger::Output(LogType::Engine,
		"Raytracingダウンサンプリング: {}",
		GetEnabledText(runtimeFeatures_.useRaytracingDownsampling));
}

void Engine::GraphicsFeatureController::SetSoftShadowSampleCount(
	uint32_t count) {

	// シェーダーが持つ分散サンプル数へ揃える
	count = count <= 1u ? 1u : count <= 2u ? 2u : 4u;
	if (preferences_.softShadowSampleCount == count) {
		return;
	}
	preferences_.softShadowSampleCount = count;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();
	Logger::Output(LogType::Engine,
		"ソフトシャドウのサンプル数: {}", count);
}

void Engine::GraphicsFeatureController::SetAllowFrustumCulling(bool enabled) {

	// カリング系はGPU機能に依存しないため、ユーザー設定をそのまま反映する
	if (preferences_.allowFrustumCulling == enabled) {
		return;
	}

	preferences_.allowFrustumCulling = enabled;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "視錐台カリング: {}", GetEnabledText(runtimeFeatures_.useFrustumCulling));
}

void Engine::GraphicsFeatureController::SetAllowOcclusionCulling(
	bool enabled) {

	if (preferences_.allowOcclusionCulling == enabled) {
		return;
	}

	preferences_.allowOcclusionCulling = enabled;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "オクルージョンカリング: {}",
		GetEnabledText(runtimeFeatures_.useOcclusionCulling));
}

void Engine::GraphicsFeatureController::SetUseGameViewCameraForSceneCulling(bool enabled) {

	if (preferences_.useGameViewCameraForSceneCulling == enabled) {
		return;
	}

	preferences_.useGameViewCameraForSceneCulling = enabled;
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "SceneViewのカリングカメラ: {}",
		enabled ? "GameView" : "SceneView");
}

void Engine::GraphicsFeatureController::SetAllowContributionCulling(bool enabled) {

	// VS経路とMS経路の両方で使うため、共通のRuntimeFeaturesへ反映する
	if (preferences_.allowContributionCulling == enabled) {
		return;
	}

	preferences_.allowContributionCulling = enabled;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "寄与度カリング: {}",
		GetEnabledText(runtimeFeatures_.useContributionCulling));
}

void Engine::GraphicsFeatureController::SetAllowNormalConeCulling(bool enabled) {

	// NormalConeはMeshShader経路専用だが、ON/OFFはメニュー側から保持しておく
	if (preferences_.allowNormalConeCulling == enabled) {
		return;
	}

	preferences_.allowNormalConeCulling = enabled;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "法線コーンカリング: {}",
		GetEnabledText(runtimeFeatures_.useNormalConeCulling));
}

void Engine::GraphicsFeatureController::SetAllowMeshLOD(
	bool enabled) {

	if (preferences_.allowMeshLOD == enabled) {
		return;
	}

	preferences_.allowMeshLOD = enabled;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();

	Logger::Output(LogType::Engine, "メッシュLOD: {}",
		GetEnabledText(runtimeFeatures_.useMeshLOD));
}

void Engine::GraphicsFeatureController::SetMeshLODThresholds(
	float lod0, float lod1, float lod2) {

	const std::array<float, 3> thresholds =
		GraphicsMeshLOD::ClampPixelThresholds(
			lod0, lod1, lod2);
	lod0 = thresholds[0];
	lod1 = thresholds[1];
	lod2 = thresholds[2];
	if (preferences_.meshLOD0PixelThreshold == lod0 &&
		preferences_.meshLOD1PixelThreshold == lod1 &&
		preferences_.meshLOD2PixelThreshold == lod2) {
		return;
	}

	preferences_.meshLOD0PixelThreshold = lod0;
	preferences_.meshLOD1PixelThreshold = lod1;
	preferences_.meshLOD2PixelThreshold = lod2;
	RebuildRuntimeFeatures();
	SavePreferencesToConfig();
}

void Engine::GraphicsFeatureController::SetFrameContextCount(
	uint32_t count) {

	count = std::clamp(count, 1u, 3u);
	if (preferences_.frameContextCount == count) {
		return;
	}

	preferences_.frameContextCount = count;
	SavePreferencesToConfig();
	Logger::Output(LogType::Engine,
		"Frame Context数: {} 再起動後に反映されます", count);
}

void Engine::GraphicsFeatureController::SetDisplayOutputMode(
	DisplayOutputMode mode) {

	if (preferences_.displayOutput.mode == mode) {
		return;
	}
	preferences_.displayOutput.mode = mode;
	SavePreferencesToConfig();
	Logger::Output(LogType::Engine,
		"表示出力: {} 再起動後に反映されます",
		EnumAdapter<DisplayOutputMode>::ToString(mode));
}

void Engine::GraphicsFeatureController::SetDisplayLuminance(
	float paperWhiteNits, float maxLuminanceNits) {

	paperWhiteNits = std::clamp(paperWhiteNits, 80.0f, 1000.0f);
	maxLuminanceNits = std::clamp(maxLuminanceNits,
		paperWhiteNits, 10000.0f);
	if (preferences_.displayOutput.paperWhiteNits == paperWhiteNits &&
		preferences_.displayOutput.maxLuminanceNits == maxLuminanceNits) {
		return;
	}
	preferences_.displayOutput.paperWhiteNits = paperWhiteNits;
	preferences_.displayOutput.maxLuminanceNits = maxLuminanceNits;
	SavePreferencesToConfig();
}

void Engine::GraphicsFeatureController::ClampPreferencesToSupport() {

	if (!support_.SupportsMeshShaderPath()) {

		preferences_.allowMeshShader = false;
	}
	if (!support_.SupportsRayTracingPath()) {

		preferences_.allowInlineRayTracing = false;
		preferences_.allowDispatchRays = false;
	}
}

void Engine::GraphicsFeatureController::RebuildRuntimeFeatures() {

	runtimeFeatures_ = GraphicsFeatureSelection::Resolve(support_, preferences_);
}

void Engine::GraphicsFeatureController::LogCurrentState() const {

	// 起動時/設定変更時に、最終的に使用される描画機能をまとめて確認できるようにする
	const double vramGB = static_cast<double>(adapterInfo_.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

	Logger::BeginSection(LogType::Engine);

	Logger::Output(LogType::Engine, "GPUアダプター: {}", adapterInfo_.adapterName);
	Logger::Output(LogType::Engine, "機能レベル: {}", GraphicsFeatureText::ToString(adapterInfo_.featureLevel));
	Logger::Output(LogType::Engine, "シェーダーモデル: {}", GraphicsFeatureText::ToString(support_.highestShaderModel));
	Logger::Output(LogType::Engine, "メッシュシェーダーTier: {}", GraphicsFeatureText::ToString(support_.meshShaderTier));
	Logger::Output(LogType::Engine, "レイトレーシングTier: {}", GraphicsFeatureText::ToString(support_.raytracingTier));
	Logger::Output(LogType::Engine, "Wave命令: {}", support_.waveOps ? "対応" : "未対応");
	Logger::Output(LogType::Engine, "専用VRAM: {:.2f} GB", vramGB);
	Logger::Output(LogType::Engine, "実行時Mesh Shader: {}", GetEnabledText(runtimeFeatures_.useMeshShader));
	Logger::Output(LogType::Engine, "実行時Inline RayTracing: {}", GetEnabledText(runtimeFeatures_.useInlineRayTracing));
	Logger::Output(LogType::Engine, "実行時DispatchRays: {}", GetEnabledText(runtimeFeatures_.useDispatchRays));
	Logger::Output(LogType::Engine, "実行時Raytracingダウンサンプリング: {}",
		GetEnabledText(runtimeFeatures_.useRaytracingDownsampling));
	Logger::Output(LogType::Engine, "ソフトシャドウのサンプル数: {}",
		runtimeFeatures_.softShadowSampleCount);
	Logger::Output(LogType::Engine, "実行時RayScene構築: {}", GetEnabledText(runtimeFeatures_.UsesAnyRayTracing()));
	Logger::Output(LogType::Engine, "実行時視錐台カリング: {}", GetEnabledText(runtimeFeatures_.useFrustumCulling));
	Logger::Output(LogType::Engine, "実行時オクルージョンカリング: {}", GetEnabledText(runtimeFeatures_.useOcclusionCulling));
	Logger::Output(LogType::Engine, "SceneViewのカリングカメラ: {}",
		preferences_.useGameViewCameraForSceneCulling ? "GameView" : "SceneView");
	Logger::Output(LogType::Engine, "実行時寄与度カリング: {}", GetEnabledText(runtimeFeatures_.useContributionCulling));
	Logger::Output(LogType::Engine, "実行時法線コーンカリング: {}", GetEnabledText(runtimeFeatures_.useNormalConeCulling));
	Logger::Output(LogType::Engine, "実行時メッシュLOD: {} ({:.1f}, {:.1f}, {:.1f})",
		GetEnabledText(runtimeFeatures_.useMeshLOD),
		runtimeFeatures_.meshLOD0PixelThreshold,
		runtimeFeatures_.meshLOD1PixelThreshold,
		runtimeFeatures_.meshLOD2PixelThreshold);
	Logger::Output(LogType::Engine, "Frame Context数: {}",
		preferences_.frameContextCount);
	Logger::Output(LogType::Engine, "表示出力: {} ({:.0f}/{:.0f} nits)",
		EnumAdapter<DisplayOutputMode>::ToString(
			preferences_.displayOutput.mode),
		preferences_.displayOutput.paperWhiteNits,
		preferences_.displayOutput.maxLuminanceNits);

	Logger::EndSection(LogType::Engine);
}

void Engine::GraphicsFeatureController::LoadPreferencesFromConfig() {

	GraphicsPreferenceStorage::Load(preferences_);
}

void Engine::GraphicsFeatureController::SavePreferencesToConfig() const {

	GraphicsPreferenceStorage::Save(preferences_);
}
