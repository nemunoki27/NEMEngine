#include "BackendDrawCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

// c++
#include <variant>

//============================================================================
//	BackendDrawCommon classMethods
//============================================================================
const  Engine::MaterialPassBinding* Engine::BackendDrawCommon::FindFirstPass(const MaterialAsset& material,
	const std::initializer_list<MaterialPassKind>& passKinds) {

	for (MaterialPassKind passKind : passKinds) {
		if (const auto* pass = FindPass(material, passKind)) {
			return pass;
		}
	}
	return nullptr;
}

bool Engine::BackendDrawCommon::ResolveMaterialPass(const RenderDrawContext& context,
	AssetID requestedMaterial, DefaultMaterialSlot defaultSlot,
	const std::initializer_list<MaterialPassKind>& passKinds,
	ResolvedMaterialPass& outResolved) {

	// 要求マテリアルが無ければデフォルトマテリアルへフォールバックする
	AssetID resolvedMaterialID = context.materialResolver->ResolveORDefault(
		*context.assetDatabase, requestedMaterial, defaultSlot);
	if (!resolvedMaterialID) {
		return false;
	}

	// マテリアルアセットをロード
	const MaterialAsset* material = context.assetLibrary->LoadMaterial(resolvedMaterialID);
	if (!material) {
		return false;
	}

	// 指定候補のパスを順に探す
	const MaterialPassBinding* pass = FindFirstPass(*material, passKinds);
	if (!pass) {
		return false;
	}

	// 結果を出力
	outResolved.materialID = resolvedMaterialID;
	outResolved.material = material;
	outResolved.pass = pass;
	return true;
}

const Engine::PipelineState* Engine::BackendDrawCommon::ResolveGraphicsPipeline(
	const RenderDrawContext& context, const MaterialPassBinding& passBinding,
	const PipelineVariantDesc** outVariant, bool forceDepthTestWrite,
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	const PipelineVariantKind desiredKind = context.forceVertexMeshVariant ?
		PipelineVariantKind::GraphicsVertex :
		passBinding.preferredVariant;
	if (passBinding.shaderOverride) {
		return context.pipelineCache->GetORCreateComposed(context.graphicsCore->GetDXObject(), *context.assetLibrary,
			passBinding.pipeline, passBinding.pipeline, passBinding.shaderOverride, desiredKind,
			context.GetRTVFormats(), context.dsvFormat, context.runtimeFeatures, outVariant);
	}
	return context.pipelineCache->GetORCreate(context.graphicsCore->GetDXObject(), *context.assetLibrary,
		passBinding.pipeline, desiredKind, context.GetRTVFormats(), context.dsvFormat,
		context.runtimeFeatures, outVariant, forceDepthTestWrite, samplerOverrides);
}

const Engine::PipelineState* Engine::BackendDrawCommon::ResolveComposedGraphicsPipeline(
	const RenderDrawContext& context, const MaterialPassBinding& passBinding, AssetID geometryPipeline,
	PipelineVariantKind desiredKind, const PipelineVariantDesc** outVariant) {

	return context.pipelineCache->GetORCreateComposed(context.graphicsCore->GetDXObject(), *context.assetLibrary,
		passBinding.pipeline, geometryPipeline, passBinding.shaderOverride, desiredKind,
		context.GetRTVFormats(), context.dsvFormat, context.runtimeFeatures, outVariant);
}

ID3D12GraphicsCommandList6* Engine::BackendDrawCommon::SetupGraphicsPipeline(const RenderDrawContext& context,
	const PipelineState& pipelineState, BlendMode blendMode) {

	auto* dxCommand = context.graphicsCore->GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// パイプラインを設定
	commandList->SetGraphicsRootSignature(pipelineState.GetRootSignature());
	commandList->SetPipelineState(pipelineState.GetGraphicsPipeline(blendMode));

	return commandList;
}

void Engine::BackendDrawCommon::PrepareGraphicsBindItemsScratch(const RenderBufferRegistry& globalRegistry,
	size_t localNamedBufferCount, size_t extraDescriptorCount, std::vector<GraphicsBindItem>& outBindItems) {

	outBindItems.clear();
	outBindItems.reserve(globalRegistry.GetCount() * 2 + localNamedBufferCount + extraDescriptorCount);
}

void Engine::BackendDrawCommon::AppendGraphicsCBV(const PipelineState& pipelineState,
	std::string_view name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress, std::vector<GraphicsBindItem>& outBindItems) {

	if (gpuAddress == 0) {
		return;
	}
	if (!pipelineState.FindBindingByName(name, ShaderBindingKind::CBV)) {
		return;
	}
	outBindItems.push_back({ .name = name,.type = GraphicsBindValueType::CBV,.gpuAddress = gpuAddress, });
}

void Engine::BackendDrawCommon::AppendGraphicsSRV(const PipelineState& pipelineState,
	std::string_view name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress, D3D12_GPU_DESCRIPTOR_HANDLE descriptor,
	std::vector<GraphicsBindItem>& outBindItems) {

	if (gpuAddress == 0 && descriptor.ptr == 0) {
		return;
	}
	if (!pipelineState.FindBindingByName(name, ShaderBindingKind::SRV)) {
		return;
	}
	outBindItems.push_back({ .name = name,.type = GraphicsBindValueType::SRV,
		.gpuAddress = gpuAddress,.descriptor = descriptor });
}

void Engine::BackendDrawCommon::AppendGraphicsBufferBindings(const RenderBufferRegistry& registry,
	const PipelineState& pipelineState, std::vector<GraphicsBindItem>& outBindItems) {

	// 登録された全ての描画バッファを取得
	const auto& entries = registry.GetEntries();
	outBindItems.reserve(outBindItems.size() + entries.size() * 3);
	for (const RegisteredRenderBuffer& entry : entries) {

		// CBV
		if (pipelineState.FindBindingByName(entry.alias, ShaderBindingKind::CBV)) {
			if (entry.gpuAddress != 0) {

				outBindItems.push_back({ entry.alias,GraphicsBindValueType::CBV,entry.gpuAddress, });
			}
		}
		// SRV
		if (pipelineState.FindBindingByName(entry.alias, ShaderBindingKind::SRV)) {
			if (entry.gpuAddress != 0 || entry.srvGPUHandle.ptr != 0) {

				outBindItems.push_back({ entry.alias,GraphicsBindValueType::SRV,
					entry.gpuAddress,entry.srvGPUHandle, });
			}
		}
		if (pipelineState.FindBindingByName(entry.alias, ShaderBindingKind::AccelStruct)) {
			if (entry.gpuAddress != 0) {

				outBindItems.push_back({ entry.alias,GraphicsBindValueType::AccelStruct,entry.gpuAddress, });
			}
		}
	}
}

const Engine::GPUTextureResource* Engine::BackendDrawCommon::ResolveTextureAsset(
	const RenderDrawContext& context, GraphicsCore& graphicsCore, AssetID textureAssetID,
	TextureColorSpace requestedColorSpace) {

	return RuntimeTextureResolver::Resolve(graphicsCore, context.assetDatabase,
		textureAssetID, requestedColorSpace);
}

Engine::MaterialParameterBufferBuilder::TextureResolveResult
Engine::BackendDrawCommon::ResolveMaterialTextureIndex(
	const RenderDrawContext& context, MaterialParameterSemantic semantic,
	const AssetID& textureAssetID) {

	if (!textureAssetID || !context.graphicsCore) {
		return {};
	}
	const RuntimeTextureResolver::BindlessResolveResult result =
		RuntimeTextureResolver::ResolveBindless(
			*context.graphicsCore, context.assetDatabase,
			textureAssetID, IsSRGBMaterialTexture(semantic) ?
			TextureColorSpace::SRGB : TextureColorSpace::Linear);
	uint32_t textureIndex = result.srvIndex;
	if (semantic == MaterialParameterSemantic::DisplacementTexture) {

		const BuiltinTextureLibrary& builtinTextures =
			context.graphicsCore->GetBuiltinTextureLibrary();
		const GPUTextureResource* errorTexture =
			builtinTextures.GetErrorTexture();
		const GPUTextureResource* neutralTexture =
			builtinTextures.GetNeutralDisplacementTexture();
		if (neutralTexture && neutralTexture->srvIndex != UINT32_MAX &&
			(result.retry ||
				(errorTexture && textureIndex == errorTexture->srvIndex))) {

			textureIndex = neutralTexture->srvIndex;
		}
	}
	return {
		.index = textureIndex,
		.cacheable = !result.retry,
	};
}

void Engine::BackendDrawCommon::BindMaterialTextures(const RenderDrawContext& context,
	const PipelineState& pipelineState, MaterialParameterBinder& binder, const MaterialAsset& material,
	ID3D12GraphicsCommandList* commandList,
	const MaterialParameterSet* overrides) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	const GPUTextureResource* whiteTexture = graphicsCore.GetBuiltinTextureLibrary().GetWhiteTexture();
	const GPUTextureResource* neutralDisplacementTexture =
		graphicsCore.GetBuiltinTextureLibrary().GetNeutralDisplacementTexture();
	const GPUTextureResource* errorTexture =
		graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();

	for (const MaterialParameterBinder::TextureBinding& textureBinding :
		binder.ResolveTextures(pipelineState, material, overrides)) {

		// 未指定や解決失敗はSemanticごとの中立テクスチャへフォールバックする
		const GPUTextureResource* texture = textureBinding.textureID ?
			ResolveTextureAsset(context, graphicsCore, textureBinding.textureID,
				IsSRGBMaterialTexture(textureBinding.semantic) ?
				TextureColorSpace::SRGB : TextureColorSpace::Linear) : nullptr;
		if (textureBinding.semantic ==
			MaterialParameterSemantic::DisplacementTexture &&
			texture == errorTexture) {

			texture = nullptr;
		}
		const GPUTextureResource* fallback =
			textureBinding.semantic == MaterialParameterSemantic::DisplacementTexture ?
			neutralDisplacementTexture : whiteTexture;
		D3D12_GPU_DESCRIPTOR_HANDLE handle =
			(texture && texture->gpuHandle.ptr != 0) ? texture->gpuHandle :
			(fallback ? fallback->gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE{});
		if (handle.ptr == 0) {
			continue;
		}
		RootBindingCommand::SetGraphicsSRV(commandList, textureBinding.rootBinding, 0, handle);
	}
}

void Engine::BackendDrawCommon::BindReflectedMaterialParameters(const RenderDrawContext& context,
	MaterialParameterBinder& binder, const PipelineState& pipelineState, const MaterialAsset& material,
	const MaterialParameterSet* overrides,
	PipelineBindingCache& bindCache, PipelineBindingCache::SlotID slot, ID3D12GraphicsCommandList* commandList) {

	// cbufferを宣言していないBuiltinシェーダーはslot未登録なので何もしない
	if (!bindCache.Has(slot)) {
		return;
	}
	static const MaterialParameterSet kEmptyOverrides{};
	const MaterialParameterSet& effectiveOverrides =
		overrides ? *overrides : kEmptyOverrides;
	ID3D12Device* device = context.graphicsCore->GetDXObject().GetDevice();
	const auto resolveTexture = [&context](MaterialParameterSemantic semantic,
		const AssetID& textureAssetID) {
		return ResolveMaterialTextureIndex(context, semantic, textureAssetID);
	};
	binder.SetTextureRevision(context.graphicsCore->GetTextureUploadService().GetContentRevision());
	const D3D12_GPU_VIRTUAL_ADDRESS materialParamsAddress =
		binder.ResolveAndUpload(context.graphicsCore->GetDXObject().GetResourceRetirement(), device, pipelineState, material,
			effectiveOverrides, resolveTexture);
	if (materialParamsAddress != 0) {
		RootBindingCommand::SetGraphicsCBV(commandList, bindCache.Get(slot), materialParamsAddress);
	}
}

bool Engine::BackendDrawCommon::CanBatchBasic(const RenderItem& first, const RenderItem& next) {

	return first.sortingLayer == next.sortingLayer &&
		first.sortingOrder == next.sortingOrder &&
		first.cameraDomain == next.cameraDomain &&
		first.orderedUI == next.orderedUI &&
		first.material == next.material &&
		first.blendMode == next.blendMode &&
		first.surfaceMode == next.surfaceMode &&
		first.batchKey == next.batchKey;
}
