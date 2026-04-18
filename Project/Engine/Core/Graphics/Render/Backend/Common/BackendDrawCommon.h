#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Asset/MaterialAsset.h>
#include <Engine/Core/Graphics/Material/MaterialResolver.h>
#include <Engine/Core/Graphics/Pipeline/PipelineStateCache.h>
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/Texture/GPUTextureResource.h>
#include <Engine/Core/Graphics/Texture/TextureUploadService.h>
#include <Engine/Core/Graphics/Texture/BuiltinTextureLibrary.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/GPUBuffer/RenderBufferRegistry.h>

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