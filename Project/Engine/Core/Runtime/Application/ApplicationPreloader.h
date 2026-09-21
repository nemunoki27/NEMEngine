#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <functional>

namespace Engine {

	class GraphicsCore;
	class AssetDatabase;
	class SceneSystem;
	class RenderPipelineRunner;
	class SkinnedMeshAnimationManager;
	class AnimationClipManager;
	struct SystemContext;
	class WorldManager;
	class SceneInstanceManager;
	class RuntimeWorldBaker;
	class ECSWorld;

	// 事前読み込み中だけ借用する実行環境
	struct ApplicationPreloadContext {

		AssetDatabase& assetDatabase;
		SceneSystem& sceneSystem;
		RenderPipelineRunner& renderPipeline;
		SkinnedMeshAnimationManager& skinnedAnimationManager;
		AnimationClipManager& animationClipManager;
		SystemContext& systemContext;
		WorldManager& worldManager;
		SceneInstanceManager& playScenes;
		RuntimeWorldBaker& runtimeWorldBaker;
		AssetID activeScene;
		std::function<void()> refreshActiveWorldContext;
	};

	//============================================================================
	//	ApplicationPreloader class
	//	起動時のアセット取得とシーン描画資源を準備する
	//============================================================================
	class ApplicationPreloader {
	public:

		// Release構成の事前読み込みを実行する
		static bool Run(GraphicsCore& graphicsCore, ApplicationPreloadContext& context, bool editor);
	private:

		// 更新せずにシーンの描画資源を確定する
		static void Warmup(GraphicsCore& graphicsCore, ECSWorld& world, SceneInstanceManager& scenes,
			SystemContext& context, ApplicationPreloadContext& preload, bool editor);
	};
}
