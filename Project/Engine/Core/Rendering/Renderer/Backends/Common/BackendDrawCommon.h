#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Core/D3D12CommandContext.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Buffers/RenderBufferRegistry.h>

// c++
#include <filesystem>
#include <initializer_list>
#include <string_view>

//============================================================================
//	BackendDrawCommon namespace
//============================================================================
namespace Engine::BackendDrawCommon {

	// マテリアルパスの解決結果
	struct ResolvedMaterialPass {

		AssetID materialID{};
		const MaterialAsset* material = nullptr;
		const MaterialPassBinding* pass = nullptr;
	};

	// 指定したマテリアルパスを探す
	const MaterialPassBinding* FindFirstPass(const MaterialAsset& material,
		const std::initializer_list<std::string_view>& passNames);

	// マテリアルからパスを取得
	bool ResolveMaterialPass(const RenderDrawContext& context, AssetID requestedMaterial,
		DefaultMaterialSlot defaultSlot, const std::initializer_list<std::string_view>& passNames,
		ResolvedMaterialPass& outResolved);

	// パイプラインを取得
	const PipelineState* ResolveGraphicsPipeline(const RenderDrawContext& context,
		const MaterialPassBinding& passBinding);

	// パイプラインをセットアップしてコマンドリストを取得
	ID3D12GraphicsCommandList6* SetupGraphicsPipeline(const RenderDrawContext& context,
		const PipelineState& pipelineState, BlendMode blendMode);

	// スクラッチデータを毎回作り直さず使いまわすための準備
	void PrepareGraphicsBindItemsScratch(const RenderBufferRegistry& globalRegistry,
		size_t localNamedBufferCount, size_t extraDescriptorCount,
		std::vector<GraphicsBindItem>& outBindItems);
	// 名前付きCBV/SRVを直接バインドアイテムに積む
	void AppendGraphicsCBV(const PipelineState& pipelineState,
		std::string_view name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
		std::vector<GraphicsBindItem>& outBindItems);
	void AppendGraphicsSRV(const PipelineState& pipelineState,
		std::string_view name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE descriptor,
		std::vector<GraphicsBindItem>& outBindItems);

	// バッファレジストリから名前付きバッファをバインドアイテムへ展開する
	void AppendGraphicsBufferBindings(const RenderBufferRegistry& registry,
		const PipelineState& pipelineState, std::vector<GraphicsBindItem>& outBindItems);


	// テクスチャアセットIDからGPUテクスチャを取得、失敗したらエラーテクスチャ
	const GPUTextureResource* ResolveTextureAsset(const RenderDrawContext& context,
		GraphicsCore& graphicsCore, AssetID textureAssetID);

	// 描画アイテムがバッチ可能か
	bool CanBatchBasic(const RenderItem& first, const RenderItem& next);
} // Engine