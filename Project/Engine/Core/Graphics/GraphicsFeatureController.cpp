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

	runtimeFeatures_.useMeshShader = support_.SupportsMeshShaderPath() && preferences_.allowMeshShader;
	runtimeFeatures_.useInlineRayTracing = support_.SupportsRayTracingPath() && preferences_.allowInlineRayTracing;
	runtimeFeatures_.useDispatchRays = support_.SupportsRayTracingPath() && preferences_.allowDispatchRays;
}

void Engine::GraphicsFeatureController::LogCurrentState() const {

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

	Logger::EndSection(LogType::Engine);
}