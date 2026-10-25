#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>

// c++
#include <functional>
#include <cstdint>
#include <chrono>

namespace Engine {

	class AssetDatabase;
	class WorldManager;
	class SceneInstanceManager;
	class EditorManager;
	class GraphicsCore;
	struct SystemContext;
	struct SceneHeader;
	class ECSWorld;

	//============================================================================
	//	EditorRenderRequestBuilder class
	//	表示状態から各ビューの描画要求を作成する
	//============================================================================
	class EditorRenderRequestBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditorRenderRequestBuilder(SystemContext& systemContext, AssetDatabase& assetDatabase,
			EditorManager& editorManager, WorldManager& worldManager);

		// エディタ状態を描画要求へ変換する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header,
			SceneInstanceManager& scenes);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		DisplayOutputSettings previousGameOutput_{};
		DisplayOutputSettings previousSceneOutput_{};

		std::chrono::steady_clock::time_point nextSecondaryViewUpdate_{};
		std::chrono::steady_clock::time_point secondaryRecoveryStarted_{};
		uint64_t lastGPUFrameID_ = UINT64_MAX;
		float averagedGPUTime_ = 0.0f;
		bool secondaryViewThrottled_ = false;
		SystemContext& systemContext_;
		AssetDatabase& assetDatabase_;
		EditorManager& editorManager_;
		WorldManager& worldManager_;

		//--------- functions ----------------------------------------------------

		// GPU負荷から副Viewの更新時刻を決める
		bool ShouldRenderSecondaryView();

	};
}
