#include "GraphicsFeatureController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Logger.h>

// c++
#include <algorithm>

//============================================================================
//	GraphicsFeatureController classMethods
//============================================================================

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
		initialized_ = true;
	}
	ClampPreferencesToSupport();
	RebuildRuntimeFeatures();
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

	Logger::Output(LogType::Engine, "Mesh Shader path -> {}", runtimeFeatures_.useMeshShader ? "Enabled" : "Disabled");
}

void Engine::GraphicsFeatureController::SetAllowInlineRayTracing(bool enabled) {

	bool clamped = enabled && support_.SupportsRayTracingPath();
	if (preferences_.allowInlineRayTracing == clamped) {
		return;
	}

	preferences_.allowInlineRayTracing = clamped;
	RebuildRuntimeFeatures();

	Logger::Output(LogType::Engine, "Inline RayTracing path -> {}", runtimeFeatures_.useInlineRayTracing ? "Enabled" : "Disabled");
}

void Engine::GraphicsFeatureController::SetAllowDispatchRays(bool enabled) {

	bool clamped = enabled && support_.SupportsRayTracingPath();
	if (preferences_.allowDispatchRays == clamped) {
		return;
	}

	preferences_.allowDispatchRays = clamped;
	RebuildRuntimeFeatures();

	Logger::Output(LogType::Engine, "DispatchRays path -> {}", runtimeFeatures_.useDispatchRays ? "Enabled" : "Disabled");
}

void Engine::GraphicsFeatureController::SetAllowFrustumCulling(bool enabled) {

	// カリング系はGPU機能に依存しないため、ユーザー設定をそのまま反映する
	if (preferences_.allowFrustumCulling == enabled) {
		return;
	}

	preferences_.allowFrustumCulling = enabled;
	RebuildRuntimeFeatures();

	Logger::Output(LogType::Engine, "Frustum Culling -> {}", runtimeFeatures_.useFrustumCulling ? "Enabled" : "Disabled");
}

void Engine::GraphicsFeatureController::SetAllowContributionCulling(bool enabled) {

	// VS経路とMS経路の両方で使うため、共通のRuntimeFeaturesへ反映する
	if (preferences_.allowContributionCulling == enabled) {
		return;
	}

	preferences_.allowContributionCulling = enabled;
	RebuildRuntimeFeatures();

	Logger::Output(LogType::Engine, "Contribution Culling -> {}", runtimeFeatures_.useContributionCulling ? "Enabled" : "Disabled");
}

void Engine::GraphicsFeatureController::SetAllowNormalConeCulling(bool enabled) {

	// NormalConeはMeshShader経路専用だが、ON/OFFはメニュー側から保持しておく
	if (preferences_.allowNormalConeCulling == enabled) {
		return;
	}

	preferences_.allowNormalConeCulling = enabled;
	RebuildRuntimeFeatures();

	Logger::Output(LogType::Engine, "Normal Cone Culling -> {}", runtimeFeatures_.useNormalConeCulling ? "Enabled" : "Disabled");
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

	// GPU対応が必要な機能はsupportで絞り、カリング系は描画側で安全側に倒せるよう設定を直で反映する
	runtimeFeatures_.useMeshShader = support_.SupportsMeshShaderPath() && preferences_.allowMeshShader;
	runtimeFeatures_.useInlineRayTracing = support_.SupportsRayTracingPath() && preferences_.allowInlineRayTracing;
	runtimeFeatures_.useDispatchRays = support_.SupportsRayTracingPath() && preferences_.allowDispatchRays;
	runtimeFeatures_.useFrustumCulling = preferences_.allowFrustumCulling;
	runtimeFeatures_.useContributionCulling = preferences_.allowContributionCulling;
	runtimeFeatures_.useNormalConeCulling = preferences_.allowNormalConeCulling;
}

void Engine::GraphicsFeatureController::LogCurrentState() const {

	// 起動時/設定変更時に、最終的に使用される描画機能をまとめて確認できるようにする
	const double vramGB = static_cast<double>(adapterInfo_.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

	Logger::BeginSection(LogType::Engine);

	Logger::Output(LogType::Engine, "Adapter: {}", adapterInfo_.adapterName);
	Logger::Output(LogType::Engine, "Feature Level: {}", GraphicsFeatureText::ToString(adapterInfo_.featureLevel));
	Logger::Output(LogType::Engine, "Shader Model: {}", GraphicsFeatureText::ToString(support_.highestShaderModel));
	Logger::Output(LogType::Engine, "Mesh Shader Tier: {}", GraphicsFeatureText::ToString(support_.meshShaderTier));
	Logger::Output(LogType::Engine, "RayTracing Tier: {}", GraphicsFeatureText::ToString(support_.raytracingTier));
	Logger::Output(LogType::Engine, "Wave Ops: {}", support_.waveOps ? "Supported" : "Not Supported");
	Logger::Output(LogType::Engine, "Dedicated VRAM: {:.2f} GB", vramGB);
	Logger::Output(LogType::Engine, "Runtime Mesh Shader: {}", runtimeFeatures_.useMeshShader ? "Enabled" : "Disabled");
	Logger::Output(LogType::Engine, "Runtime Inline RayTracing: {}", runtimeFeatures_.useInlineRayTracing ? "Enabled" : "Disabled");
	Logger::Output(LogType::Engine, "Runtime DispatchRays: {}", runtimeFeatures_.useDispatchRays ? "Enabled" : "Disabled");
	Logger::Output(LogType::Engine, "Runtime RayScene Build: {}", runtimeFeatures_.UsesAnyRayTracing() ? "Enabled" : "Disabled");
	Logger::Output(LogType::Engine, "Runtime Frustum Culling: {}", runtimeFeatures_.useFrustumCulling ? "Enabled" : "Disabled");
	Logger::Output(LogType::Engine, "Runtime Contribution Culling: {}", runtimeFeatures_.useContributionCulling ? "Enabled" : "Disabled");
	Logger::Output(LogType::Engine, "Runtime Normal Cone Culling: {}", runtimeFeatures_.useNormalConeCulling ? "Enabled" : "Disabled");

	Logger::EndSection(LogType::Engine);
}
