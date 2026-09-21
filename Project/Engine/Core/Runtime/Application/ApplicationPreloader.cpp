#include "ApplicationPreloader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>

// c++
#include <algorithm>
#include <chrono>

using namespace Engine;

// c++

bool ApplicationPreloader::Run([[maybe_unused]] GraphicsCore& graphicsCore,
	[[maybe_unused]] ApplicationPreloadContext& context, [[maybe_unused]] bool editor) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	return false;
#else
	const auto startTime = std::chrono::steady_clock::now();
	Logger::Output(LogType::Engine, editor ? "[RuntimePreload] Release起動時の事前読み込みを開始します" :
		"[実行時事前読み込み] Release起動時の事前読み込みを開始します");

	context.renderPipeline.PreloadRuntimeAssets(graphicsCore, context.assetDatabase);

	std::vector<const AssetMeta*> assets;
	assets.reserve(context.assetDatabase.GetAssets().size());
	for (const auto& [assetID, meta] : context.assetDatabase.GetAssets()) {
		assets.emplace_back(&meta);
	}
	std::sort(assets.begin(), assets.end(), [](const AssetMeta* lhs, const AssetMeta* rhs) {
		return lhs->assetPath < rhs->assetPath;
		});

	std::vector<AssetID> sceneAssets;
	for (const AssetMeta* meta : assets) {
		switch (meta->type) {
		case AssetType::Mesh:
			context.skinnedAnimationManager.RequestLoadAsync(context.assetDatabase, meta->guid);
			break;
		case AssetType::AnimationClip:
			context.animationClipManager.GetOrLoad(context.assetDatabase, meta->guid);
			break;
		case AssetType::Audio:
		{
			const std::filesystem::path fullPath = context.assetDatabase.ResolveFullPath(meta->guid);
			if (!fullPath.empty()) {
				Audio::GetInstance()->EnsureLoaded(fullPath);
			}
			break;
		}
		case AssetType::Scene:
			if (meta->assetPath.starts_with("GameAssets/")) {
				sceneAssets.emplace_back(meta->guid);
			}
			break;
		default:
			break;
		}
	}
	context.skinnedAnimationManager.WaitAll();

	for (AssetID sceneAsset : sceneAssets) {
		if (sceneAsset == context.activeScene) {
			continue;
		}

		const AssetMeta* sceneMeta = editor ? context.assetDatabase.Find(sceneAsset) : nullptr;
		if (editor) {
			Logger::Output(LogType::Engine, "[RuntimePreload] シーンのWarmupを開始します path={}",
				sceneMeta ? sceneMeta->assetPath : ToString(sceneAsset));
		}
		ECSWorld warmupWorld{};
		SceneInstanceManager warmupScenes{};
		if (!warmupScenes.LoadSceneTree(context.assetDatabase, context.sceneSystem, warmupWorld, sceneAsset)) {
			if (editor) {
				Logger::Output(LogType::Engine, spdlog::level::warn,
					"[RuntimePreload] シーンの読み込みに失敗しました GUID={}", ToString(sceneAsset));
			} else {
				Logger::Output(LogType::Engine, spdlog::level::warn,
					"[実行時事前読み込み] シーンの読み込みに失敗しました GUID={}", ToString(sceneAsset));
			}
			continue;
		}

		SystemContext warmupContext{};
		warmupContext.engineContext = &graphicsCore.GetContext();
		warmupContext.graphicsPlatform = &graphicsCore.GetDXObject();
		warmupContext.assetDatabase = &context.assetDatabase;
		warmupContext.skinnedAnimationManager = &context.skinnedAnimationManager;
		warmupContext.animationClipManager = &context.animationClipManager;
		warmupContext.mode = WorldMode::Play;
		warmupContext.world = &warmupWorld;
		if (const SceneInstance* activeScene = warmupScenes.GetActive()) {
			warmupContext.activeSceneHeader = &activeScene->header;
		}

		WorldCommandServices services{};
		services.assetDatabase = &context.assetDatabase;
		services.sceneInstances = &warmupScenes;
		services.sceneSystem = &context.sceneSystem;
		warmupWorld.SetCommandServices(services);

		HierarchySystem hierarchySystem{};
		hierarchySystem.OnWorldEnter(warmupWorld, warmupContext);
		TransformSystem transformSystem{};
		transformSystem.LateUpdate(warmupWorld, warmupContext);
		Warmup(graphicsCore, warmupWorld, warmupScenes, warmupContext, context, editor);
		if (editor) {
			Logger::Output(LogType::Engine, "[RuntimePreload] シーンのWarmupが完了しました path={}",
				sceneMeta ? sceneMeta->assetPath : ToString(sceneAsset));
		}
	}

	context.systemContext.engineContext = &graphicsCore.GetContext();
	context.systemContext.graphicsPlatform = &graphicsCore.GetDXObject();
	context.systemContext.assetDatabase = &context.assetDatabase;
	context.systemContext.skinnedAnimationManager = &context.skinnedAnimationManager;
	context.systemContext.animationClipManager = &context.animationClipManager;
	if (editor) {
		context.systemContext.runtimeWorldBaker = &context.runtimeWorldBaker;
		context.systemContext.deltaTime = 0.0f;
		context.systemContext.unscaledDeltaTime = 0.0f;
	}
	context.refreshActiveWorldContext();
	if (ECSWorld* playWorld = context.worldManager.GetPlayWorld()) {
		if (editor) Logger::Output(LogType::Engine, "[RuntimePreload] 起動シーンのWarmupを開始します");
		Warmup(graphicsCore, *playWorld, context.playScenes, context.systemContext, context, editor);
		if (editor) Logger::Output(LogType::Engine, "[RuntimePreload] 起動シーンのWarmupが完了しました");
	}

	graphicsCore.GetTextureUploadService().WaitAll();
	graphicsCore.GetBufferUploadService().FlushAndWait();
	graphicsCore.GetDXObject().WaitForGPU();

	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - startTime).count();
	if (editor) {
		Logger::Output(LogType::Engine,
			"[RuntimePreload] Release起動時の事前読み込みが完了しました Scene数={} 経過={}ms", sceneAssets.size(), elapsed);
	} else {
		Logger::Output(LogType::Engine,
			"[実行時事前読み込み] Release起動時の事前読み込みが完了しました シーン数={} 経過={}ms", sceneAssets.size(), elapsed);
	}
	Logger::Flush(LogType::Engine);
	return true;
#endif
}

void ApplicationPreloader::Warmup(GraphicsCore& graphicsCore, ECSWorld& world,
	SceneInstanceManager& scenes, SystemContext& context, ApplicationPreloadContext& preload, bool editor) {

	const SceneInstance* activeScene = scenes.GetActive();
	if (!activeScene) {
		return;
	}

	context.world = &world;
	context.activeSceneHeader = &activeScene->header;
	context.deltaTime = 0.0f;
	context.unscaledDeltaTime = 0.0f;

	RenderFrameRequest request{};
	request.sceneInstances = &scenes;
	request.header = &activeScene->header;
	request.activeSceneInstanceID = activeScene->instanceID;
	request.world = &world;
	request.systemContext = &context;
	request.assetDatabase = &preload.assetDatabase;

	const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
	RenderViewRequest& gameView = request.views[static_cast<uint32_t>(RenderViewKind::Game)];
	gameView.kind = RenderViewKind::Game;
	gameView.enabled = true;
	gameView.width = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.x));
	gameView.height = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.y));
	gameView.sourceKind = RenderViewSourceKind::WorldCamera;

	RenderViewRequest& sceneView = request.views[static_cast<uint32_t>(RenderViewKind::Scene)];
	sceneView.kind = RenderViewKind::Scene;
	sceneView.enabled = false;

	if (editor) {
		sceneView.width = 0;
		sceneView.height = 0;
		Logger::Output(LogType::Engine, "[RuntimePreload] シーン描画コマンドの記録を開始します");
	}
	preload.renderPipeline.Render(graphicsCore, request);
	if (editor) Logger::Output(LogType::Engine, "[RuntimePreload] シーン描画コマンドの記録が完了しました");
	// Scene固有Bufferが破棄される前にCopy Queueを提出し、描画Queueとの依存を確定する
	graphicsCore.GetBufferUploadService().SubmitBatch();
	if (editor) Logger::Output(LogType::Engine, "[RuntimePreload] シーン描画のGPU完了待機を開始します");
	graphicsCore.GetDXObject().WaitForGPU();
	graphicsCore.GetBufferUploadService().FlushAndWait();
	if (editor) Logger::Output(LogType::Engine, "[RuntimePreload] シーン描画のGPU完了待機が完了しました");
}
