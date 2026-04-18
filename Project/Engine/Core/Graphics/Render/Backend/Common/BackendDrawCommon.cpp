#include "BackendDrawCommon.h"

//============================================================================
//	BackendDrawCommon classMethods
//============================================================================

const  Engine::MaterialPassBinding* Engine::BackendDrawCommon::FindFirstPass(const MaterialAsset& material,
	const std::initializer_list<std::string_view>& passNames) {

	for (std::string_view passName : passNames) {
		if (const auto* pass = FindPass(material, passName)) {
			return pass;
		}
	}
	return nullptr;
}

bool Engine::BackendDrawCommon::ResolveMaterialPass(const RenderDrawContext& context,
	AssetID requestedMaterial, DefaultMaterialSlot defaultSlot,
	const std::initializer_list<std::string_view>& passNames,
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
	const MaterialPassBinding* pass = FindFirstPass(*material, passNames);
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
	const RenderDrawContext& context, const MaterialPassBinding& passBinding) {

	return context.pipelineCache->GetORCreate(context.graphicsCore->GetDXObject(), *context.assetLibrary,
		passBinding.pipeline, passBinding.preferredVariant, context.GetRTVFormats(), context.dsvFormat);
}

ID3D12GraphicsCommandList6* Engine::BackendDrawCommon::SetupGraphicsPipeline(const RenderDrawContext& context,
	const PipelineState& pipelineState, BlendMode blendMode) {

	auto* dxCommand = context.graphicsCore->GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	dxCommand->SetDescriptorHeaps({ context.graphicsCore->GetSRVDescriptor().GetDescriptorHeap() });

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

	// フォールバック用のエラーテクスチャを取得
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	if (!fallback || !fallback->valid) {
		return nullptr;
	}

	// テクスチャ未指定ならエラーテクスチャ
	if (!textureAssetID || !context.assetDatabase) {
		return fallback;
	}

	// アセットIDからフルパスを取得
	std::filesystem::path fullPath = context.assetDatabase->ResolveFullPath(textureAssetID);
	if (fullPath.empty()) {
		return fallback;
	}

	// 現状の実装に合わせて、キーもアセットパスもフルパス文字列を使う
	const std::string key = fullPath.generic_string();
	graphicsCore.GetTextureUploadService().RequestTextureFile(key, key);
	if (const auto* texture = graphicsCore.GetTextureUploadService().GetTexture(key)) {
		// テクスチャが有効ならそれを返す
		if (texture->valid) {

			return texture;
		}
	}
	return fallback;
}

bool Engine::BackendDrawCommon::CanBatchBasic(const RenderItem& first, const RenderItem& next) {

	return first.sortingLayer == next.sortingLayer &&
		first.sortingOrder == next.sortingOrder &&
		first.material == next.material &&
		first.blendMode == next.blendMode &&
		first.batchKey == next.batchKey;
}