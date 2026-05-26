#include "LightCullingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/ViewLightCullingBufferSet.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	LightCullingPass classMethods
//============================================================================

namespace {

	constexpr const char* kLightCullingMaterialPath =
		"Engine/Assets/Materials/Builtin/LightCulling/lightCulling.material.json";

	void AppendComputeBufferBindings(const Engine::RenderBufferRegistry& registry,
		const Engine::PipelineState& pipelineState, std::vector<Engine::ComputeBindItem>& outBindItems) {

		const auto& entries = registry.GetEntries();
		outBindItems.reserve(outBindItems.size() + entries.size() * 4);
		for (const Engine::RegisteredRenderBuffer& entry : entries) {

			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::CBV)) {
				if (entry.gpuAddress != 0) {
					outBindItems.push_back({ entry.alias, Engine::ComputeBindValueType::CBV, entry.gpuAddress });
				}
			}
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::SRV)) {
				if (entry.gpuAddress != 0 || entry.srvGPUHandle.ptr != 0) {
					outBindItems.push_back({ entry.alias, Engine::ComputeBindValueType::SRV,
						entry.gpuAddress, entry.srvGPUHandle });
				}
			}
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::UAV)) {
				if (entry.gpuAddress != 0 || entry.uavGPUHandle.ptr != 0) {
					outBindItems.push_back({ entry.alias, Engine::ComputeBindValueType::UAV,
						entry.gpuAddress, entry.uavGPUHandle });
				}
			}
		}
	}
}

Engine::AssetID Engine::LightCullingPass::ResolveLightCullingMaterial(AssetDatabase& database) const {

	if (materialSearched_) {
		return cachedMaterialID_;
	}
	materialSearched_ = true;

	const AssetMeta* meta = database.FindByPath(kLightCullingMaterialPath);
	if (meta) {
		cachedMaterialID_ = meta->guid;
	}
	return cachedMaterialID_;
}

void Engine::LightCullingPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
	if (!context.resources || !context.assetDatabase || !deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}
	const GraphicsRuntimeFeatures& runtimeFeatures =
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	if (!runtimeFeatures.useLightCulling) {
		return;
	}

	RenderPathResources* cullingResources = context.lightCullingResources ?
		context.lightCullingResources : context.resources;
	MultiRenderTarget* sceneMain = cullingResources->GetSceneMain();
	if (!sceneMain) {
		return;
	}
	DepthTexture2D* depth = sceneMain->GetDepthTexture();
	if (!depth) {
		return;
	}

	AssetID materialID = ResolveLightCullingMaterial(*context.assetDatabase);
	if (!materialID) {
		return;
	}

	const MaterialAsset* material = deps_.assetLibrary->LoadMaterial(materialID);
	if (!material) {
		return;
	}
	const MaterialPassBinding* passBinding = FindPass(*material, "LightCulling");
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		return;
	}
	const PipelineState* pipelineState = deps_.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*deps_.assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, "[LightCullingPass] pipeline is missing.");
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// 深度をシェーダーリード用に遷移
	depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// 深度SRVをt1にバインド
	std::vector<ComputeBindItem> bindItems{};
	if (pipelineState->FindBinding(ShaderBindingKind::SRV, 1, 0)) {
		bindItems.push_back({ {}, ComputeBindValueType::SRV, 0, depth->GetSRVGPUHandle(), 1, 0 });
	}
	// バッファレジストリからライト関連バッファを自動バインド
	AppendComputeBufferBindings(context.bufferRegistry, *pipelineState, bindItems);

	ComputeRootBinder binder{ *pipelineState };
	binder.Bind(commandList, bindItems);

	const uint32_t dispatchX = DxUtils::RoundUp(sceneMain->GetWidth(), pipelineState->GetThreadGroupX());
	const uint32_t dispatchY = DxUtils::RoundUp(sceneMain->GetHeight(), pipelineState->GetThreadGroupY());
	const uint32_t dispatchZ =
		(runtimeFeatures.lightCullingMode == LightCullingMode::Clustered ||
		 runtimeFeatures.lightCullingMode == LightCullingMode::DebugAllLightsPerCluster) ?
		ViewLightCullingBufferSet::kClusterCountZ : 1u;
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);
}
