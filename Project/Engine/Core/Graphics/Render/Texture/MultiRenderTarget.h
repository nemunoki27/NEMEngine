#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Texture/RenderTexture2D.h>
#include <Engine/Core/Graphics/Render/Texture/DepthTexture2D.h>
#include <Engine/MathLib/Color.h>

// c++
#include <memory>
#include <vector>
#include <optional>
#include <string>

namespace Engine {

	// front
	class RTVDescriptor;
	class DSVDescriptor;
	class SRVDescriptor;
	class DxCommand;

	//============================================================================
	//	MultiRenderTarget structures
	//============================================================================

	// 色レンダーテクスチャの情報
	struct ColorAttachmentDesc {

		// 名前
		std::string name;
		// リソースフォーマット
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

		// クリアカラー
		Color4 clearColor = Color4::Black();
		// UAVを作成するかどうか
		bool createUAV = false;
	};
	// マルチレンダリングターゲットの生成に必要な情報
	struct MultiRenderTargetCreateDesc {

		// テクスチャのサイズ
		uint32_t width = 1;
		uint32_t height = 1;

		// 色レンダーテクスチャの情報
		std::vector<ColorAttachmentDesc> colors;
		// 深度レンダーテクスチャの情報
		std::optional<DepthTextureCreateDesc> depth;
	};
	// レンダーターゲットのクリア情報
	struct MultiRenderTargetClearDesc {
		
		// 色情報をクリアするか
		bool clearColor = true;
		std::optional<Color4> clearColorValue;
		// 深度情報をクリアするか
		bool clearDepth = false;
		float clearDepthValue = 1.0f;
		// ステンシルをクリアするか
		bool clearStencil = false;
		uint8_t clearStencilValue = 0;
	};

	//============================================================================
	//	MultiRenderTarget class
	//	複数色、深度のレンダーテクスチャをまとめて管理するクラス
	//============================================================================
	class MultiRenderTarget {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MultiRenderTarget() = default;
		~MultiRenderTarget() = default;

		// リソース作成
		void Create(ID3D12Device* device, RTVDescriptor* rtvDescriptor,
			DSVDescriptor* dsvDescriptor, SRVDescriptor* srvDescriptor,
			const MultiRenderTargetCreateDesc& desc);
		// 破棄
		void Destroy();

		// リソースの遷移
		void TransitionForRender(DxCommand& dxCommand);
		void TransitionForShaderRead(DxCommand& dxCommand);
		// レンダーターゲットのバインド
		void Bind(DxCommand& dxCommand) const;
		// レンダーターゲットのクリア
		void Clear(DxCommand& dxCommand, const MultiRenderTargetClearDesc& desc) const;

		//--------- accessor -----------------------------------------------------

		// レンダーターゲットが有効かどうか
		bool IsValid() const { return !colors_.empty(); }

		// テクスチャのサイズの取得
		uint32_t GetWidth() const { return width_; }
		uint32_t GetHeight() const { return height_; }

		// 色レンダーテクスチャの取得
		RenderTexture2D* GetColorTexture(size_t index) const { return colors_[index].get(); }
		uint32_t GetColorCount() const { return static_cast<uint32_t>(colors_.size()); }
		// 深度レンダーテクスチャの取得
		DepthTexture2D* GetDepthTexture() const { return depth_.get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 色レンダーテクスチャ
		std::vector<std::unique_ptr<RenderTexture2D>> colors_;
		// 深度レンダーテクスチャ
		std::unique_ptr<DepthTexture2D> depth_;

		// テクスチャのサイズ
		uint32_t width_ = 0;
		uint32_t height_ = 0;

		//--------- functions ----------------------------------------------------

		// レンダーターゲットの情報を構築する
		std::vector<RenderTarget> BuildRenderTargets() const;
	};
} // Engine