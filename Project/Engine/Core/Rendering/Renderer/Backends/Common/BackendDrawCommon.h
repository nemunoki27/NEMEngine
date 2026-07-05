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
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>

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
		const std::initializer_list<MaterialPassKind>& passKinds);

	// マテリアルからパスを取得
	bool ResolveMaterialPass(const RenderDrawContext& context, AssetID requestedMaterial,
		DefaultMaterialSlot defaultSlot, const std::initializer_list<MaterialPassKind>& passKinds,
		ResolvedMaterialPass& outResolved);

	// パイプラインを取得、outVariantを渡すと解決済みバリアントも受け取れる
	// forceDepthTestWriteを立てると深度テスト+書き込みを強制した別PSOを取得する
	const PipelineState* ResolveGraphicsPipeline(const RenderDrawContext& context,
		const MaterialPassBinding& passBinding, const PipelineVariantDesc** outVariant = nullptr,
		bool forceDepthTestWrite = false);

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

	// マテリアルテクスチャをreflection駆動でバインドする
	// 規約はregister space2のテクスチャSRVだけマテリアルテクスチャ扱い、エンジン供給SRVはspace0か1
	// material.parametersの同名AssetIDを解決し、未指定や失敗なら既定の白テクスチャを使う
	// space2テクスチャを宣言しないBuiltinでは何もせず無回帰
	// overridesを渡すとレンダラー個別のテクスチャ上書きをマテリアル既定より優先する
	void BindMaterialTextures(const RenderDrawContext& context, const PipelineState& pipelineState,
		const MaterialAsset& material, ID3D12GraphicsCommandList* commandList,
		const std::unordered_map<std::string, MaterialParameterValue>* overrides = nullptr);

	// MaterialParameters cbufferをreflection駆動でアップロードしバインドする
	// overridesはエンティティごとの個別マテリアル用、nullや空なら既定値のみになる
	void BindReflectedMaterialParameters(const RenderDrawContext& context, MaterialParameterBinder& binder,
		const PipelineState& pipelineState, const MaterialAsset& material,
		const std::unordered_map<std::string, MaterialParameterValue>* overrides,
		PipelineBindingCache& bindCache, PipelineBindingCache::SlotID slot,
		ID3D12GraphicsCommandList* commandList);

	// 描画アイテムがバッチ可能か
	bool CanBatchBasic(const RenderItem& first, const RenderItem& next);
} // Engine
