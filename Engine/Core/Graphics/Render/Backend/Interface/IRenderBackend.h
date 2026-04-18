#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/GraphicsFeatureTypes.h>

// c++
#include <span>
#include <array>

namespace Engine {

	// front
	class GraphicsCore;
	class RenderSceneBatch;
	struct ResolvedRenderView;
	struct SystemContext;
	class AssetDatabase;
	class RenderAssetLibrary;
	class PipelineStateCache;
	class MaterialResolver;
	class RenderBufferRegistry;

	//============================================================================
	//	IRenderBackend structures
	//============================================================================

	// 描画コンテキスト
	struct RenderDrawContext {

		GraphicsCore* graphicsCore = nullptr;
		const ResolvedRenderView* view = nullptr;
		const SystemContext* systemContext = nullptr;
		const RenderSceneBatch* batch = nullptr;
		const RenderBufferRegistry* bufferRegistry = nullptr;
		AssetDatabase* assetDatabase = nullptr;
		RenderAssetLibrary* assetLibrary = nullptr;
		PipelineStateCache* pipelineCache = nullptr;
		MaterialResolver* materialResolver = nullptr;

		// 現在バインド中の描画先フォーマット
		std::array<DXGI_FORMAT, 8> rtvFormats{};
		uint32_t numRTVFormats = 0;
		DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;

		// 描画パスの名前
		std::string_view passName = "Draw";
		bool depthOnly = false;

		std::span<const DXGI_FORMAT> GetRTVFormats() const { return std::span<const DXGI_FORMAT>(rtvFormats.data(), numRTVFormats); }
	};

	//============================================================================
	//	IRenderBackend class
	//	描画インターフェース
	//============================================================================
	class IRenderBackend {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IRenderBackend() = default;
		virtual ~IRenderBackend() = default;

		// フレーム開始
		virtual void BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {}

		// 描画アイテムの描画
		virtual void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) = 0;
		// 描画アイテムが同一バッチにまとめられるか
		virtual bool CanBatch(const RenderItem& first, const RenderItem& next, const GraphicsRuntimeFeatures& features) const = 0;

		//--------- accessor -----------------------------------------------------

		// 描画IDを取得
		virtual uint32_t GetID() const = 0;
	};
} // Engine