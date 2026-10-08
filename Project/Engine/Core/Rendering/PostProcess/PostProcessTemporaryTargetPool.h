#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <string>

// directX
#include <dxgiformat.h>

namespace Engine {

	// 前方宣言
	class GraphicsCore;
	class MultiRenderTarget;
	class RenderTargetRegistry;

	//============================================================================
	//	PostProcessTemporaryTargetPool structures
	//============================================================================
	// ポストエフェクト実行設定
	struct PostProcessTemporaryTargetDesc {

		std::string name;
		DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		float widthScale = 1.0f;
		float heightScale = 1.0f;
		bool createUAV = true;
	};

	//============================================================================
	//	PostProcessTemporaryTargetPool class
	// PostProcessの中間出力先をRenderTargetRegistryへ登録する補助クラス
	//============================================================================
	class PostProcessTemporaryTargetPool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PostProcessTemporaryTargetPool() = default;
		~PostProcessTemporaryTargetPool() = default;

		// sourceと同じサイズのUAV付き一時RTを取得する
		MultiRenderTarget* Acquire(GraphicsCore& graphicsCore, RenderTargetRegistry& registry,
			const std::string& name, const MultiRenderTarget& source);
		// サイズ・形式・UAV設定に合わせて中間RTを取得する
		MultiRenderTarget* Acquire(GraphicsCore& graphicsCore, RenderTargetRegistry& registry,
			const PostProcessTemporaryTargetDesc& desc, const MultiRenderTarget& source);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// GPU形式をSceneの形式へ変換する
		static SceneRenderTargetFormat ToSceneFormat(DXGI_FORMAT format);
	};
}

