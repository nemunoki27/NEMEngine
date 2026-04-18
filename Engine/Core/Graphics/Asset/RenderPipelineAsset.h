#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/GraphicsFeatureTypes.h>
#include <Engine/Core/Asset/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	RenderPipelineAsset structures
	//============================================================================

	// パイプラインバリアントの種類
	enum class PipelineVariantKind :
		uint8_t {

		GraphicsVertex,
		GraphicsGeometry,
		GraphicsMesh,
		Compute,
		Raytracing,
	};

	// パイプラインを構築するための記述子
	struct PipelineVariantDesc {

		/// パイプラインの種類
		PipelineVariantKind kind = PipelineVariantKind::GraphicsVertex;
		PipelineType pipelineType = PipelineType::Vertex;
		// 使用されるシェーダーアセット
		AssetID shader{};

		// パイプラインステートの記述子
		D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		D3D12_DEPTH_STENCIL_DESC depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		DXGI_SAMPLE_DESC sampleDesc{ 1, 0 };
		DXGI_FORMAT dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		std::vector<DXGI_FORMAT> rtvFormats;
		uint32_t numRenderTargets = 1;
		std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;

		// レンダーターゲットのフォーマットが動的かどうか
		bool dynamicRenderTargetFormats = false;
		// メッシュシェーダーを使用するかどうか
		bool requiresMeshShader = false;
		// レイトレーシングを使用するかどうか
		bool requiresInlineRayTracing = false;
		bool requiresDispatchRays = false;

		// レイトレーシング用のシェーダー
		std::string rayGenerationExport;
		std::string missExport;
		std::string closestHitExport;
		std::string anyHitExport;
		std::string hitGroupExport = "DefaultHitGroup";

		// レイトレーシングのパラメータ
		uint32_t maxPayloadSizeInBytes = 16;
		uint32_t maxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
		uint32_t maxRecursionDepth = 1;
	};

	// レンダーパイプラインアセットの情報
	struct RenderPipelineAsset {

		// アセットID
		AssetID guid{};
		// パイプラインの名前
		std::string name;

		// 使用されるパイプラインリスト
		std::vector<PipelineVariantDesc> variants;
	};

	// json変換
	bool FromJson(const nlohmann::json& data, RenderPipelineAsset& outAsset);
	nlohmann::json ToJson(const RenderPipelineAsset& asset);

	// パイプラインバリアントを検索する
	const PipelineVariantDesc* ResolveBestVariant(const RenderPipelineAsset& asset,
		PipelineVariantKind desiredKind, const GraphicsRuntimeFeatures& runtimeFeatures);
} // Engine