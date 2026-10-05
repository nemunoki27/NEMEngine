#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

void Engine::RenderPipelineRunner::CaptureProfileConditions(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	auto& profiler = FrameProfiler::GetInstance();
	if (!profiler.IsCaptureRecording()) { return; }
	if (profileCaptureRevision_ != profiler.GetCaptureRevision()) {
		// 入力の収集時間は描画の性能比較へ混ぜない
		profileInputSnapshot_ = request.assetDatabase ?
			ProfileInputSnapshotBuilder::Capture(*request.assetDatabase, *request.world, request.sceneInstances) : ProfileInputSnapshot{};
		profileWorldLifetime_ = request.world->GetLifetime();
		profileAssetLifetime_ = request.assetDatabase ? request.assetDatabase->GetCacheLifetime() : std::weak_ptr<const uint8_t>{};
		profileSceneRevision_ = request.sceneInstances ? request.sceneInstances->GetRevision() : 0;
		profileCaptureRevision_ = profiler.GetCaptureRevision();
		profiler.SkipCaptureFrame();
	}
	// GPU能力と実際に選ばれた描画経路を分けて記録する
	const auto& controller = graphicsCore.GetDXObject().GetFeatureController();
	const auto& features = controller.GetRuntimeFeatures();
	const auto& preferences = controller.GetPreferences();
	nlohmann::json conditions = {
		{ "gpu", controller.GetAdapterInfo().adapterName }, { "shaderModel", controller.GetSupport().highestShaderModel },
		{ "driverVersion", controller.GetAdapterInfo().driverVersion },
		{ "videoMemoryBytes", controller.GetAdapterInfo().dedicatedVideoMemoryBytes },
		{ "assetSHA256", profileInputSnapshot_.assetSHA256 }, { "worldSHA256", profileInputSnapshot_.worldSHA256 },
		{ "inputSnapshotComplete", profileInputSnapshot_.complete && request.assetDatabase &&
			profileAssetLifetime_.lock() == request.assetDatabase->GetCacheLifetime().lock() &&
			profileWorldLifetime_.lock() == request.world->GetLifetime() &&
			profileSceneRevision_ == (request.sceneInstances ? request.sceneInstances->GetRevision() : 0) &&
			ProfileInputSnapshotBuilder::HasSameAssetRevisions(*request.assetDatabase, profileInputSnapshot_) },
		{ "assetFileCount", profileInputSnapshot_.fileCount },
		{ "entityCount", profileInputSnapshot_.entityCount },
		{ "scriptProfiler", ScriptProfiler::GetInstance().IsEnabled() },
		{ "scriptDetailType", ScriptProfiler::GetInstance().DetailType() },
		{ "scriptDetailOwner", ScriptProfiler::GetInstance().DetailOwner() },
		{ "worldMode", request.systemContext ? static_cast<uint32_t>(request.systemContext->mode) : 0u },
		{ "meshShader", features.useMeshShader }, { "raytracingShadow", features.useInlineRayTracing },
		{ "raytracingReflection", features.useDispatchRays }, { "raytracingDownsampling", features.useRaytracingDownsampling },
		{ "shadowSamples", features.softShadowSampleCount }, { "meshLOD", features.useMeshLOD },
		{ "lodThresholds", { features.meshLOD0PixelThreshold, features.meshLOD1PixelThreshold, features.meshLOD2PixelThreshold } },
		{ "frustumCulling", features.useFrustumCulling }, { "occlusionCulling", features.useOcclusionCulling },
		{ "contributionCulling", features.useContributionCulling }, { "normalConeCulling", features.useNormalConeCulling },
		{ "frameContextCount", GraphicsFrameState::GetActiveCount() }, { "sceneUsesGameCamera", preferences.useGameViewCameraForSceneCulling },
		{ "sceneGrid", request.drawSceneViewDefaultGrid }, { "cameraBounds", request.drawSceneView2DCameraBounds },
		{ "scenes", nlohmann::json::array() }, { "views", nlohmann::json::array() }
	};
#if defined(_DEBUG)
	conditions["configuration"] = "Debug";
#elif defined(_DEVELOPBUILD)
	conditions["configuration"] = "Develop";
#else
	conditions["configuration"] = "Release";
#endif
	if (request.sceneInstances) {
		for (const auto& scene : request.sceneInstances->GetAll()) { conditions["scenes"].push_back(ToString(scene.sceneAsset)); }
	}
	const auto appendView = [&](const ResolvedRenderView& view) {
		if (!view.valid) { return; }
		const ResolvedCameraView& camera = view.perspective.valid ? view.perspective : view.orthographic;
		const auto* sceneObject = request.world->TryGetComponent<SceneObjectComponent>(camera.sourceCamera);
		RenderFeatureProfileAsset profile;
		profile.colorPipeline = camera.colorPipeline;
		// 実行計画が参照したPass設定を記録する
		if (camera.renderPasses) {
			const auto& executed = GetCameraState(view).renderPassesRuntime.GetProfile();
			profile.passes = executed.passes;
			profile.hierarchy = executed.hierarchy;
		}
		nlohmann::json settings = ToJson(profile);
		settings.erase("guid");
		settings.erase("name");
		// 2Dと3Dの投影と視点を別々に残す
		nlohmann::json cameras = nlohmann::json::array();
		for (const auto* resolved : { &view.orthographic, &view.perspective, &view.screen }) {
			if (!resolved->valid) { continue; }
			const auto* object = request.world->TryGetComponent<SceneObjectComponent>(resolved->sourceCamera);
			cameras.push_back({ { "camera", object ? ToString(object->localFileID) : std::string("manual") },
				{ "projection", static_cast<uint32_t>(resolved->projectionMode) }, { "mask", resolved->cullingMask },
				{ "position", { resolved->cameraPos.x, resolved->cameraPos.y, resolved->cameraPos.z } },
				{ "forward", { resolved->forward.x, resolved->forward.y, resolved->forward.z } },
				{ "viewMatrix", resolved->matrices.viewMatrix.m }, { "projectionMatrix", resolved->matrices.projectionMatrix.m } });
		}
		conditions["views"].push_back({ { "kind", static_cast<uint32_t>(view.kind) },
			{ "cameras", std::move(cameras) },
			{ "camera", sceneObject ? ToString(sceneObject->localFileID) : std::string("manual") },
			{ "width", view.width }, { "height", view.height }, { "target", ToString(view.targetTexture) },
			{ "outputRect", { view.outputX, view.outputY, view.outputWidth, view.outputHeight } },
			{ "mask", camera.cullingMask }, { "projection", static_cast<uint32_t>(camera.projectionMode) },
			{ "nearClip", camera.nearClip }, { "farClip", camera.farClip },
			{ "postProcess", camera.postProcessEnabled }, { "postProcessSettings", std::move(settings) } });
	};
	for (const auto& view : gameCameraViews_) { appendView(view); }
	appendView(sceneViewState_.view);
	FrameProfiler::GetInstance().SetConditions(conditions.dump());
}
