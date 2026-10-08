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

	// 前方宣言
	class GraphicsCore;

	//============================================================================
	//	ScreenSpaceOutlineViewResources structure
	//	ViewごとにOutlineの中間描画先を保持する
	//============================================================================
	struct ScreenSpaceOutlineViewResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ScreenSpaceOutlineViewResources() = default;
		ScreenSpaceOutlineViewResources(const ScreenSpaceOutlineViewResources&) = delete;
		ScreenSpaceOutlineViewResources& operator=(const ScreenSpaceOutlineViewResources&) = delete;
		ScreenSpaceOutlineViewResources(ScreenSpaceOutlineViewResources&&) noexcept = default;
		ScreenSpaceOutlineViewResources& operator=(ScreenSpaceOutlineViewResources&&) noexcept = default;

		// 全描画先が有効か調べる
		bool IsValid() const;
		// 中間描画先を破棄する
		void Destroy();

		//--------- accessor -----------------------------------------------------

		MultiRenderTarget* GetMask() { return mask_.get(); }
		const MultiRenderTarget* GetMask() const { return mask_.get(); }
		MultiRenderTarget* GetProjectedCoverageMask() { return projectedCoverageMask_.get(); }
		const MultiRenderTarget* GetProjectedCoverageMask() const { return projectedCoverageMask_.get(); }
		MultiRenderTarget* GetHorizontalDilatedMask() { return horizontalDilatedMask_.get(); }
		const MultiRenderTarget* GetHorizontalDilatedMask() const { return horizontalDilatedMask_.get(); }
		MultiRenderTarget* GetDilatedMask() { return dilatedMask_.get(); }
		const MultiRenderTarget* GetDilatedMask() const { return dilatedMask_.get(); }
	private:
		friend class RenderPathResources;
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// SceneDepth付きで描く選択対象のMask
		std::unique_ptr<MultiRenderTarget> mask_;
		// 遮蔽物の内周を除くための投影範囲
		std::unique_ptr<MultiRenderTarget> projectedCoverageMask_;
		// 横方向と最終の輪郭膨張結果
		std::unique_ptr<MultiRenderTarget> horizontalDilatedMask_;
		std::unique_ptr<MultiRenderTarget> dilatedMask_;
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
		void Resize(const RenderTargetCreationContext& context, uint32_t width, uint32_t height);

		// 破棄
		void Destroy();

		//--------- accessor -----------------------------------------------------

		// 有効か
		bool IsValid() const;

		// Deferred用のGBufferと深度
		MultiRenderTarget* GetSceneMain() { return sceneMain_.get(); }
		const MultiRenderTarget* GetSceneMain() const { return sceneMain_.get(); }
		// Lightingと後段描画の合成先
		MultiRenderTarget* GetSceneFinal() { return sceneFinal_.get(); }
		const MultiRenderTarget* GetSceneFinal() const { return sceneFinal_.get(); }
		MultiRenderTarget* GetSceneColorOpaque() { return sceneColorOpaque_.get(); }
		const MultiRenderTarget* GetSceneColorOpaque() const { return sceneColorOpaque_.get(); }
		// 深度プリパスから生成するHi-Zテクスチャ
		DepthPyramidTexture& GetDepthPyramid() { return depthPyramid_; }
		const DepthPyramidTexture& GetDepthPyramid() const { return depthPyramid_; }

		// GBuffer各アタッチメントの取得、ライティングパスが属性ごとに参照する
		RenderTexture2D* GetGBufferAlbedo() { return GetGBufferColor(GBufferAttachment::Albedo); }
		const RenderTexture2D* GetGBufferAlbedo() const { return GetGBufferColor(GBufferAttachment::Albedo); }
		RenderTexture2D* GetGBufferNormal() { return GetGBufferColor(GBufferAttachment::Normal); }
		const RenderTexture2D* GetGBufferNormal() const { return GetGBufferColor(GBufferAttachment::Normal); }
		RenderTexture2D* GetGBufferPosition() { return GetGBufferColor(GBufferAttachment::Position); }
		const RenderTexture2D* GetGBufferPosition() const { return GetGBufferColor(GBufferAttachment::Position); }
		RenderTexture2D* GetGBufferMaterial() { return GetGBufferColor(GBufferAttachment::Material); }
		const RenderTexture2D* GetGBufferMaterial() const { return GetGBufferColor(GBufferAttachment::Material); }
		RenderTexture2D* GetGBufferEmissive() { return GetGBufferColor(GBufferAttachment::Emissive); }
		const RenderTexture2D* GetGBufferEmissive() const { return GetGBufferColor(GBufferAttachment::Emissive); }
		RenderTexture2D* GetGBufferFlags() { return GetGBufferColor(GBufferAttachment::Flags); }
		const RenderTexture2D* GetGBufferFlags() const { return GetGBufferColor(GBufferAttachment::Flags); }
		RenderTexture2D* GetGBufferMotion() { return GetGBufferColor(GBufferAttachment::Motion); }
		const RenderTexture2D* GetGBufferMotion() const { return GetGBufferColor(GBufferAttachment::Motion); }
		// 属性を動的に選んで取得する、GBufferデバッグ表示などで使う
		RenderTexture2D* GetGBuffer(GBufferAttachment attachment) { return GetGBufferColor(attachment); }
		const RenderTexture2D* GetGBuffer(GBufferAttachment attachment) const { return GetGBufferColor(attachment); }
		// Runtime Component用のScreen-space Outline中間RT
		ScreenSpaceOutlineViewResources& GetRuntimeScreenSpaceOutline() { return runtimeOutline_; }
		const ScreenSpaceOutlineViewResources& GetRuntimeScreenSpaceOutline() const { return runtimeOutline_; }
		// Editor選択表示用のScreen-space Outline中間RT
		ScreenSpaceOutlineViewResources& GetEditorSelectionScreenSpaceOutline();
		const ScreenSpaceOutlineViewResources& GetEditorSelectionScreenSpaceOutline() const;

		// DeferredのGBufferと深度の作成情報を構築する
		static MultiRenderTargetCreateDesc BuildSceneMainDesc(uint32_t width, uint32_t height);
		// Lightingと後段描画の合成先を構築する
		static MultiRenderTargetCreateDesc BuildSceneFinalDesc(uint32_t width, uint32_t height);
		// Outlineの整数Maskの作成情報を構築する
		static MultiRenderTargetCreateDesc BuildScreenSpaceOutlineMaskDesc(
			uint32_t width, uint32_t height, std::string_view name, bool createUAV);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		uint32_t currentWidth_ = 0;
		uint32_t currentHeight_ = 0;

		// GBufferと深度の描画先
		std::unique_ptr<MultiRenderTarget> sceneMain_;
		// UAV付きの合成先
		std::unique_ptr<MultiRenderTarget> sceneFinal_;
		// SceneFinalと同形式の透明Surface用読み取りコピー
		std::unique_ptr<MultiRenderTarget> sceneColorOpaque_;
		DepthPyramidTexture depthPyramid_{};
		// Runtime ScreenSpaceOutlineComponent用
		ScreenSpaceOutlineViewResources runtimeOutline_{};
		// Editor選択temporary request用
		ScreenSpaceOutlineViewResources editorSelectionOutline_{};

		//--------- functions ----------------------------------------------------

		// 寸法と全資源の状態から再生成を判定する
		bool NeedsResize(uint32_t width, uint32_t height) const;
		// 公開前の描画資源をまとめて生成する
		void Create(const RenderTargetCreationContext& context, uint32_t width, uint32_t height);
		// 完成した資源と確定寸法を入れ替える
		void Swap(RenderPathResources& other) noexcept;

		// GBufferの指定アタッチメントを取得する、未生成や範囲外はnullptr
		RenderTexture2D* GetGBufferColor(GBufferAttachment attachment);
		const RenderTexture2D* GetGBufferColor(GBufferAttachment attachment) const;

		// 透明描画が読む背景の作成情報を構築する
		static MultiRenderTargetCreateDesc BuildSceneColorOpaqueDesc(uint32_t width, uint32_t height);
	};
}

