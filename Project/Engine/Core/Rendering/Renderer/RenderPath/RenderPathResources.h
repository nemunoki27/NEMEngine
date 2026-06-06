#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

// c++
#include <cstdint>
#include <memory>
#include <string_view>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ScreenSpaceOutlineViewResources structure
	// Screen-space outlineで使うview単位の中間RT
	// RuntimeとEditor選択で混線しないよう、RenderPathResources内で別々に持つ
	//============================================================================
	struct ScreenSpaceOutlineViewResources {

		// SceneDepth付きで描く、実際に見えている選択対象のMask
		std::unique_ptr<MultiRenderTarget> mask;

		// Depth Test無しで描く、選択対象本来の画面投影範囲
		// ExteriorPreferred時に遮蔽物由来の内周を除外するために使う
		std::unique_ptr<MultiRenderTarget> projectedCoverageMask;

		// Separable Dilationの中間出力(横方向のみDilate済み)
		std::unique_ptr<MultiRenderTarget> horizontalDilatedMask;
		std::unique_ptr<MultiRenderTarget> dilatedMask;

		bool IsValid() const;
		void Destroy();
	};

	//============================================================================
	//	RenderPathResources class
	//	固定RenderPathが使用するView単位の中間レンダーターゲットを管理するクラス
	//============================================================================
	class RenderPathResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
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
		// Runtime Component用のScreen-space Outline中間RT
		ScreenSpaceOutlineViewResources& GetRuntimeScreenSpaceOutline() { return runtimeOutline_; }
		const ScreenSpaceOutlineViewResources& GetRuntimeScreenSpaceOutline() const { return runtimeOutline_; }
		// Editor選択表示用のScreen-space Outline中間RT
		ScreenSpaceOutlineViewResources& GetEditorSelectionScreenSpaceOutline() { return editorSelectionOutline_; }
		const ScreenSpaceOutlineViewResources& GetEditorSelectionScreenSpaceOutline() const { return editorSelectionOutline_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- variables ----------------------------------------------------

		uint32_t currentWidth_ = 0;
		uint32_t currentHeight_ = 0;

		// SceneColorMain + SceneNormalMain + ScenePositionMain + Depth
		std::unique_ptr<MultiRenderTarget> sceneMain_;
		// SceneColorFinal (UAV付き)
		std::unique_ptr<MultiRenderTarget> sceneFinal_;
		// Runtime ScreenSpaceOutlineComponent用
		ScreenSpaceOutlineViewResources runtimeOutline_{};
		// Editor選択temporary request用
		ScreenSpaceOutlineViewResources editorSelectionOutline_{};

		//--------- functions ----------------------------------------------------

		static MultiRenderTargetCreateDesc BuildSceneMainDesc(uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildSceneFinalDesc(uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildScreenSpaceOutlineMaskDesc(
			uint32_t width, uint32_t height, std::string_view name, bool createUAV);
	};
} // Engine
