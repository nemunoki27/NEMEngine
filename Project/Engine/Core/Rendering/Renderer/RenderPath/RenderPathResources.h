#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

// c++
#include <cstdint>
#include <memory>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	RenderPathResources class
	//	固定RenderPathが使用するView単位の中間レンダーターゲットを管理するクラス
	//============================================================================
	class RenderPathResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderPathResources() = default;
		~RenderPathResources() = default;

		// コピー禁止
		RenderPathResources(const RenderPathResources&) = delete;
		RenderPathResources& operator=(const RenderPathResources&) = delete;

		// サイズに応じてレンダーターゲットを生成/再生成する
		void Resize(GraphicsCore& graphicsCore, uint32_t width, uint32_t height);

		// 破棄
		void Destroy();

		//--------- accessor -----------------------------------------------------

		// 有効か
		bool IsValid() const { return sceneMain_ && sceneMain_->IsValid() && sceneFinal_ && sceneFinal_->IsValid(); }

		// Opaque/DepthPrepass/LightCulling用の3色+深度サーフェス
		MultiRenderTarget* GetSceneMain() const { return sceneMain_.get(); }
		// Raytracing/Transparent/PostProcess用の1色(UAV)サーフェス
		MultiRenderTarget* GetSceneFinal() const { return sceneFinal_.get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		uint32_t currentWidth_ = 0;
		uint32_t currentHeight_ = 0;

		// SceneColorMain + SceneNormalMain + ScenePositionMain + Depth
		std::unique_ptr<MultiRenderTarget> sceneMain_;
		// SceneColorFinal (UAV付き)
		std::unique_ptr<MultiRenderTarget> sceneFinal_;

		//--------- functions ----------------------------------------------------

		static MultiRenderTargetCreateDesc BuildSceneMainDesc(uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildSceneFinalDesc(uint32_t width, uint32_t height);
	};
} // Engine
