#include "ApplicationPreloader.h"
#include "RuntimeAssetPreloadPlan.h"
#include "RuntimeAssetPreloadRequests.h"

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

// c++
#include <chrono>

using namespace Engine;

bool ApplicationPreloader::Run([[maybe_unused]] GraphicsCore& graphicsCore,
	[[maybe_unused]] ApplicationPreloadContext& context, [[maybe_unused]] bool editor) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	return false;
#else
	const auto startTime = std::chrono::steady_clock::now();
	Logger::Output(LogType::Engine, editor ? "[RuntimePreload] Release起動時の事前読み込みを開始します" :
		"[実行時事前読み込み] Release起動時の事前読み込みを開始します");

	// 開いているSceneとその依存だけを準備する
	std::vector<AssetID> roots;
	for (const SceneInstance& scene : context.playScenes.GetAll()) {
		if (scene.sceneAsset) { roots.push_back(scene.sceneAsset); }
	}
	if (roots.empty() && context.activeScene) { roots.push_back(context.activeScene); }
	PreloadAssets(graphicsCore, context, roots);
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
			"[RuntimePreload] Release起動時の事前読み込みが完了しました Scene数={} 経過={}ms", roots.size(), elapsed);
	} else {
		Logger::Output(LogType::Engine,
			"[実行時事前読み込み] Release起動時の事前読み込みが完了しました シーン数={} 経過={}ms", roots.size(), elapsed);
	}
	Logger::Flush(LogType::Engine);
	return true;
#endif
}

bool ApplicationPreloader::ProcessRequests(GraphicsCore& graphicsCore, ApplicationPreloadContext& context) {

	ECSWorld* world = context.systemContext.world;
	auto* requests = world ? world->GetStorage().TryGet<RuntimeAssetPreloadRequests>() : nullptr;
	if (!requests) { return false; }
	std::vector<AssetID> roots = requests->Take();
	if (roots.empty()) { return false; }
	PreloadAssets(graphicsCore, context, roots);
	return true;
}

void ApplicationPreloader::PreloadAssets(GraphicsCore& graphicsCore, ApplicationPreloadContext& context,
	std::span<const AssetID> roots) {

	RuntimeAssetPreloadPlan plan = RuntimeAssetPreloadPlan::Collect(context.assetDatabase, roots);
	for (AssetID missing : plan.missing) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "[実行時事前読み込み] 参照先がありません GUID={}", ToString(missing));
	}
	// ScriptやEntityを生成せず共有Assetだけを準備する
	for (AssetID asset : plan.assets) {
		const AssetMeta& meta = *context.assetDatabase.Find(asset);
		switch (meta.type) {
		case AssetType::Mesh:
			context.skinnedAnimationManager.RequestLoadAsync(context.assetDatabase, asset);
			break;
		case AssetType::AnimationClip:
			context.animationClipManager.GetOrLoad(context.assetDatabase, asset);
			break;
		case AssetType::Audio:
			Audio::GetInstance()->EnsureLoaded(context.assetDatabase.ResolveFullPath(asset));
			break;
		default:
			break;
		}
	}
	context.skinnedAnimationManager.WaitAll();
	context.renderPipeline.PreloadRuntimeAssets(graphicsCore, context.assetDatabase, plan.assets);
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
