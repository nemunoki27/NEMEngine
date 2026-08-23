#include "SpriteRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>

// c++
#include <algorithm>

//============================================================================
//	SpriteRenderBackend internal
//============================================================================
namespace {

	bool IsOutlineMaskPass(Engine::MaterialPassKind passKind) {

		return passKind == Engine::MaterialPassKind::ScreenSpaceOutlineMask ||
			passKind == Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask;
	}

	bool ResolveSpriteOutlinePass(
		const Engine::RenderDrawContext& context,
		Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		const Engine::AssetID materialID =
			Engine::BuiltinAssets::Materials::SpriteOutlineMask;
		const Engine::MaterialAsset* material =
			context.assetLibrary->LoadMaterial(materialID);
		if (!material) {
			return false;
		}
		const Engine::MaterialPassBinding* pass =
			Engine::FindPass(*material, context.passKind);
		if (!pass) {
			return false;
		}
		outResolved.materialID = materialID;
		outResolved.material = material;
		outResolved.pass = pass;
		return true;
	}

	// Spriteのマテリアル値からベースカラーテクスチャを解決する
	Engine::AssetID ResolveBaseColorTexture(
		const Engine::MaterialParameterSet* instance,
		const Engine::MaterialAsset& material) {

		const Engine::MaterialParameterValue* value = instance ?
			instance->Find(Engine::MaterialParameterSemantic::BaseColorTexture) : nullptr;
		if (!value) {
			value = material.parameters.Find(
				Engine::MaterialParameterSemantic::BaseColorTexture);
		}
		const Engine::AssetID* texture = value ?
			std::get_if<Engine::AssetID>(&value->value) : nullptr;
		return texture ? *texture : Engine::AssetID{};
	}

	// Importer設定を静的サンプラー上書きへ変換する
	Engine::PipelineStaticSamplerSettings MakeSamplerSettings(
		const Engine::TextureImportSettings& settings) {

		Engine::PipelineStaticSamplerSettings sampler{};
		sampler.filter = Engine::ToD3D12Filter(settings);
		sampler.addressU = Engine::ToD3D12AddressMode(settings.addressU);
		sampler.addressV = Engine::ToD3D12AddressMode(settings.addressV);
		sampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.maxAnisotropy = (std::clamp)(settings.maxAnisotropy, 1u, 16u);
		return sampler;
	}
}

//============================================================================
//	SpriteRenderBackend classMethods
//============================================================================
Engine::SpriteRenderBackend::~SpriteRenderBackend() {

	// FrameBatchResourcePool内のunique_ptrを終了時に明示resetする
	resourcePool_.Clear();
}

void Engine::SpriteRenderBackend::BeginFrame([[maybe_unused]] GraphicsCore& graphicsCore) {

	// フレーム開始時にプールをリセットする
	resourcePool_.BeginFrame();
	BeginFrameCommon();
}

void Engine::SpriteRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;

	// バッチ描画に使用するリソースを取得する
	SpriteBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](SpriteBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});

	// マテリアルパスを解決する
	BackendDrawCommon::ResolvedMaterialPass sourcePass{};
	if (!BackendDrawCommon::ResolveMaterialPass(
		context, items.front()->material, DefaultMaterialSlot::Sprite,
		{ MaterialPassKind::Draw }, sourcePass)) {
		return;
	}
	BackendDrawCommon::ResolvedMaterialPass resolvedPass = sourcePass;
	if (IsOutlineMaskPass(context.passKind) &&
		!ResolveSpriteOutlinePass(context, resolvedPass)) {
		return;
	}
	const MaterialAsset* bindingMaterial = sourcePass.material;
	const SpriteRenderPayload* firstPayload = context.batch->GetPayload<SpriteRenderPayload>(*items.front());
	PipelineStaticSamplerOverrideSet samplerOverrides{};
	const PipelineStaticSamplerOverrideSet* samplerOverridesPtr = nullptr;
	if (!resolvedPass.pass->shaderOverride && bindingMaterial) {

		const AssetID textureAsset = ResolveBaseColorTexture(
			firstPayload ? firstPayload->materialInstance : nullptr,
			*bindingMaterial);
		if (textureAsset) {

			const TextureImportSettings settings =
				RuntimeTextureResolver::ResolveImportSettings(
					context.assetDatabase, textureAsset);
			samplerOverrides.byName["gSampler"] = MakeSamplerSettings(settings);
			samplerOverridesPtr = &samplerOverrides;
		}
	}
	// パイプラインを解決する
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(
		context, *resolvedPass.pass, nullptr, false, samplerOverridesPtr);
	if (!pipelineState) {
		return;
	}

	// GPUリソースの更新
	resources.UpdateView(*context.view, items.front()->cameraDomain);
	resources.UploadInstances(*context.view, *context.batch, items);

	// パイプラインを設定
	ID3D12GraphicsCommandList* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, items.front()->blendMode);

	// IAステージ設定
	{
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &resources.GetVBV());
		commandList->IASetIndexBuffer(&resources.GetIBV());
	}
	// ルートパラメータをバインド
	{
		// レジストリのオートバインドとスロット解決をまとめて行う
		SyncAndBindRegistry(*pipelineState, context, commandList);
		if (perDrawBindCache_.Has(viewCBVSlot_)) {
			RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_),
				resources.GetViewGPUAddress());
		}
		if (perDrawBindCache_.Has(vsInstSRVSlot_) && resources.GetInstanceVSGPUAddress() != 0) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(vsInstSRVSlot_),
				resources.GetInstanceVSGPUAddress(), {});
		}
		if (perDrawBindCache_.Has(psInstSRVSlot_) && resources.GetInstancePSGPUAddress() != 0) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(psInstSRVSlot_),
				resources.GetInstancePSGPUAddress(), {});
		}
		// overrides持ちはCanBatchで単独描画になるので先頭の上書きを使う
		if (bindingMaterial) {
			BackendDrawCommon::BindReflectedMaterialParameters(context, materialParamBinder_, *pipelineState,
				*bindingMaterial, firstPayload ? firstPayload->materialInstance : nullptr,
				perDrawBindCache_, materialParamsCBVSlot_, commandList);
		}
		// space2のマテリアルテクスチャをreflection駆動でバインドする
		if (bindingMaterial) {
			BackendDrawCommon::BindMaterialTextures(context, *pipelineState, materialParamBinder_,
				*bindingMaterial, commandList,
				firstPayload ? firstPayload->materialInstance : nullptr);
		}
		if (perDrawBindCache_.Has(outlineMaskCBVSlot_)) {

			ScreenSpaceOutlineMaskConstants maskConstants{};
			maskConstants.styleID = context.screenSpaceOutlineMaskStyleID;
			maskConstants.restrictSubMeshIndex =
				context.screenSpaceOutlineMaskRestrictSubMeshIndex;
			maskConstants.alphaSource = context.screenSpaceOutlineMaskAlphaSource;
			const PostProcessConstantBufferAllocation maskAlloc =
				constantBufferAllocator_.AllocateAndUpload(
					graphicsCore.GetDXObject().GetDevice(), maskConstants);
			RootBindingCommand::SetGraphicsCBV(
				commandList, perDrawBindCache_.Get(outlineMaskCBVSlot_),
				maskAlloc.gpuAddress);
		}
	}

	// インスタンシングで描画
	commandList->DrawIndexedInstanced(6, resources.GetInstanceCount(), 0, 0, 0);
}
