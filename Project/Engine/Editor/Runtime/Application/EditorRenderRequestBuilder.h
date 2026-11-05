#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>

namespace Engine {

	class AssetDatabase;
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
			EditorManager& editorManager);

		// エディタ状態を描画要求へ変換する
		RenderFrameRequest BuildRenderFrameRequest(GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header,
			SceneInstanceManager& scenes);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		SystemContext& systemContext_;
		AssetDatabase& assetDatabase_;
		EditorManager& editorManager_;

	};
}
