#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthPyramidTexture.h>

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
	//	GBufferAttachment enum
	//	SceneMainのcolorアタッチメント並びと一致させるDeferred GBufferのインデックス
	//============================================================================
	enum class GBufferAttachment : uint32_t {

		Albedo = 0,
		Normal = 1,
		Position = 2,
		Material = 3,
		Emissive = 4,
		Flags = 5,
		Motion = 6,
		Count
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
		bool IsValid() const {

			return sceneMain_ && sceneMain_->IsValid() &&
				sceneFinal_ && sceneFinal_->IsValid() &&
				sceneColorOpaque_ && sceneColorOpaque_->IsValid();
		}

		// DeferredのGBufferサーフェス、color並びはGBufferAttachmentと一致させる
		// color0 albedo / color1 normal / color2 worldPosition / color3 material / color4 emissive / color5 flags + 深度
		// color0は移行中ライティング済み色を保持し、color2のworldPositionは将来depth復元へ置換予定
		MultiRenderTarget* GetSceneMain() const { return sceneMain_.get(); }
		// Raytracing/Transparent/PostProcess用の1色(UAV)サーフェス、ライティング結果の合成先
		MultiRenderTarget* GetSceneFinal() const { return sceneFinal_.get(); }
		MultiRenderTarget* GetSceneColorOpaque() const { return sceneColorOpaque_.get(); }
		// 深度プリパスから生成するHi-Zテクスチャ
		DepthPyramidTexture& GetDepthPyramid() { return depthPyramid_; }
		const DepthPyramidTexture& GetDepthPyramid() const { return depthPyramid_; }

		// GBuffer各アタッチメントの取得、ライティングパスが属性ごとに参照する
		RenderTexture2D* GetGBufferAlbedo() const { return GetGBufferColor(GBufferAttachment::Albedo); }
		RenderTexture2D* GetGBufferNormal() const { return GetGBufferColor(GBufferAttachment::Normal); }
		RenderTexture2D* GetGBufferPosition() const { return GetGBufferColor(GBufferAttachment::Position); }
		RenderTexture2D* GetGBufferMaterial() const { return GetGBufferColor(GBufferAttachment::Material); }
		RenderTexture2D* GetGBufferEmissive() const { return GetGBufferColor(GBufferAttachment::Emissive); }
		RenderTexture2D* GetGBufferFlags() const { return GetGBufferColor(GBufferAttachment::Flags); }
		RenderTexture2D* GetGBufferMotion() const { return GetGBufferColor(GBufferAttachment::Motion); }
		// 属性を動的に選んで取得する、GBufferデバッグ表示などで使う
		RenderTexture2D* GetGBuffer(GBufferAttachment attachment) const { return GetGBufferColor(attachment); }
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
		// SceneFinalと同形式の透明Surface用読み取りコピー
		std::unique_ptr<MultiRenderTarget> sceneColorOpaque_;
		DepthPyramidTexture depthPyramid_{};
		// Runtime ScreenSpaceOutlineComponent用
		ScreenSpaceOutlineViewResources runtimeOutline_{};
		// Editor選択temporary request用
		ScreenSpaceOutlineViewResources editorSelectionOutline_{};

		//--------- functions ----------------------------------------------------

		// GBufferの指定アタッチメントを取得する、未生成や範囲外はnullptr
		RenderTexture2D* GetGBufferColor(GBufferAttachment attachment) const {

			const uint32_t index = static_cast<uint32_t>(attachment);
			if (!sceneMain_ || index >= sceneMain_->GetColorCount()) {
				return nullptr;
			}
			return sceneMain_->GetColorTexture(index);
		}

		static MultiRenderTargetCreateDesc BuildSceneMainDesc(uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildSceneFinalDesc(uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildSceneColorOpaqueDesc(uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildScreenSpaceOutlineMaskDesc(
			uint32_t width, uint32_t height, std::string_view name, bool createUAV);
	};
} // Engine

