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
	const PipelineVariantDesc** outVariant, bool forceDepthTestWrite) {

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
		context.runtimeFeatures, outVariant, forceDepthTestWrite);
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
	const RenderDrawContext& context, GraphicsCore& graphicsCore, AssetID textureAssetID) {

	return RuntimeTextureResolver::Resolve(graphicsCore, context.assetDatabase, textureAssetID);
}

void Engine::BackendDrawCommon::BindMaterialTextures(const RenderDrawContext& context,
	const PipelineState& pipelineState, const MaterialAsset& material,
	ID3D12GraphicsCommandList* commandList,
	const std::unordered_map<std::string, MaterialParameterValue>* overrides) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	const GPUTextureResource* whiteTexture = graphicsCore.GetBuiltinTextureLibrary().GetWhiteTexture();

	const ShaderReflectionInfo& reflection = pipelineState.GetGraphicsReflection();
	for (const ShaderResourceBinding& resource : reflection.resources) {

		// マテリアルテクスチャの規約はspace2のテクスチャSRVだけ、それ以外はエンジンが供給する
		if (resource.kind != ShaderBindingKind::SRV || resource.space != 2 ||
			resource.rawType != D3D_SIT_TEXTURE) {
			continue;
		}
		const RootBindingLocation* binding =
			pipelineState.FindBinding(ShaderBindingKind::SRV, resource.bindPoint, resource.space);
		if (!binding) {
			continue;
		}

		// 同名のmaterial paramからテクスチャのAssetIDを引く、上書きがあれば優先する
		AssetID textureID{};
		if (overrides) {
			auto overrideIt = overrides->find(resource.name);
			if (overrideIt != overrides->end()) {
				if (const AssetID* id = std::get_if<AssetID>(&overrideIt->second.value)) {
					textureID = *id;
				}
			}
		}
		if (!textureID) {
			auto found = material.parameters.find(resource.name);
			if (found != material.parameters.end()) {
				if (const AssetID* id = std::get_if<AssetID>(&found->second.value)) {
					textureID = *id;
				}
			}
		}

		// 未指定や解決失敗は白テクスチャを使い、テクスチャ無しでも破綻させない
		const GPUTextureResource* texture = textureID ?
			ResolveTextureAsset(context, graphicsCore, textureID) : nullptr;
		D3D12_GPU_DESCRIPTOR_HANDLE handle = (texture && texture->gpuHandle.ptr != 0) ?
			texture->gpuHandle : (whiteTexture ? whiteTexture->gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE{});
		if (handle.ptr == 0) {
			continue;
		}
		RootBindingCommand::SetGraphicsSRV(commandList, binding, 0, handle);
	}
}

void Engine::BackendDrawCommon::BindReflectedMaterialParameters(const RenderDrawContext& context,
	MaterialParameterBinder& binder, const PipelineState& pipelineState, const MaterialAsset& material,
	const std::unordered_map<std::string, MaterialParameterValue>* overrides,
	PipelineBindingCache& bindCache, PipelineBindingCache::SlotID slot, ID3D12GraphicsCommandList* commandList) {

	// cbufferを宣言していないBuiltinシェーダーはslot未登録なので何もしない
	if (!bindCache.Has(slot)) {
		return;
	}
	static const std::unordered_map<std::string, MaterialParameterValue> kEmptyOverrides{};
	const std::unordered_map<std::string, MaterialParameterValue>& effectiveOverrides =
		overrides ? *overrides : kEmptyOverrides;
	ID3D12Device* device = context.graphicsCore->GetDXObject().GetDevice();
	const D3D12_GPU_VIRTUAL_ADDRESS materialParamsAddress =
		binder.ResolveAndUpload(device, pipelineState, material, effectiveOverrides);
	if (materialParamsAddress != 0) {
		RootBindingCommand::SetGraphicsCBV(commandList, bindCache.Get(slot), materialParamsAddress);
	}
}

bool Engine::BackendDrawCommon::CanBatchBasic(const RenderItem& first, const RenderItem& next) {

	return first.sortingLayer == next.sortingLayer &&
		first.sortingOrder == next.sortingOrder &&
		first.material == next.material &&
		first.blendMode == next.blendMode &&
		first.batchKey == next.batchKey;
}
