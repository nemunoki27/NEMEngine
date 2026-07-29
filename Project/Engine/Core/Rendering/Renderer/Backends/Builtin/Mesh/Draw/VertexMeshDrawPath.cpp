#include "VertexMeshDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthPyramidTexture.h>
#include <Engine/Core/Rendering/DxObject/Buffers/RenderBufferRegistry.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	VertexMeshDrawPath classMethods
//============================================================================
Engine::VertexMeshDrawPath::VertexMeshDrawPath() {

	// IndirectArgs生成Compute用スロットを初期化時に登録する
	indirectArgsCBVSlot_     = indirectArgsBindCache_.AddSlot("IndirectArgsConstants",  ShaderBindingKind::CBV);
	iaViewCBVSlot_           = indirectArgsBindCache_.AddSlot("ViewConstants",           ShaderBindingKind::CBV);
	iaDrawCBVSlot_           = indirectArgsBindCache_.AddSlot("MeshDrawConstants",       ShaderBindingKind::CBV);
	iaMeshInstSRVSlot_       = indirectArgsBindCache_.AddSlot("gMeshInstances",          ShaderBindingKind::SRV);
	iaSubMeshSRVSlot_        = indirectArgsBindCache_.AddSlot("gSubMeshes",              ShaderBindingKind::SRV);
	occlusionDepthSRVSlot_   = indirectArgsBindCache_.AddSlot("gOcclusionDepthPyramid", ShaderBindingKind::SRV);
	visibleInstUAVSlot_      = indirectArgsBindCache_.AddSlot("gVisibleMeshInstances",   ShaderBindingKind::UAV);
	idxIndirectArgsUAVSlot_  = indirectArgsBindCache_.AddSlot("gIndexedIndirectArgs",    ShaderBindingKind::UAV);

	// Draw時:カリング済みインスタンス配列をgMeshInstancesとして再バインドする
	drawMeshInstSRVSlot_ = drawBindCache_.AddSlot("gMeshInstances", ShaderBindingKind::SRV);
}

bool Engine::VertexMeshDrawPath::Supports(const PipelineVariantDesc& variant) const {

	return variant.kind == PipelineVariantKind::GraphicsVertex ||
		variant.kind == PipelineVariantKind::GraphicsGeometry;
}

void Engine::VertexMeshDrawPath::EnsureCommandSignature(ID3D12Device* device) {

	if (commandSignature_) {
		return;
	}

	// 1コマンドでDrawIndexedInstanced相当の引数だけを読む
	D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
	argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

	D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
	signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
	signatureDesc.NumArgumentDescs = 1;
	signatureDesc.pArgumentDescs = &argumentDesc;

	HRESULT hr = device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&commandSignature_));
	Assert::Call(SUCCEEDED(hr), "CreateCommandSignature failed");
}

void Engine::VertexMeshDrawPath::Setup(const MeshPathSetupContext& context) {

	const auto& prepared = *context.prepared;

	context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.commandList->IASetIndexBuffer(&prepared.gpuMesh->indexBuffer.GetIndexBufferView());
}

void Engine::VertexMeshDrawPath::Draw(const MeshPathDrawContext& context) {

	const auto& prepared = *context.prepared;

	ID3D12PipelineState* graphicsPipeline =
		prepared.pipelineState->GetGraphicsPipeline(
			prepared.items.front()->blendMode);
	context.commandList->SetPipelineState(graphicsPipeline);

	// Pickingはクリック時だけ実行するため、LOD0の全インスタンスを直接描画する
	// Indirectカリング用バッファを介さず元のEntity IDをそのままPSへ渡す
	if (context.drawContext->passKind ==
		MaterialPassKind::EditorPicking) {

		context.commandList->DrawIndexedInstanced(
			prepared.gpuMesh->lods[0].indexCount,
			prepared.instanceCount,
			prepared.gpuMesh->lods[0].indexOffset,
			0, 0);
		return;
	}

	EnsureCommandSignature(context.graphicsCore->GetDXObject().GetDevice());
	// Index数は固定、Instance数や可視インスタンス配列はComputeで決定する
	prepared.resources->UpdateIndexedIndirectArgsConstants(prepared.gpuMesh->indexCount);
	if (!BuildIndexedIndirectArgs(context)) {
		return;
	}

	// Indirect引数生成でCompute PSOへ切り替わるため描画前にGraphics PSOへ戻す
	context.commandList->SetPipelineState(graphicsPipeline);

	// VS側はLOD領域ごとにカリング済みインスタンス配列をgMeshInstancesとして読む
	drawBindCache_.Sync(*prepared.pipelineState);
	if (!drawBindCache_.Has(drawMeshInstSRVSlot_)) {
		return;
	}

	const RootBindingLocation* instanceBinding =
		drawBindCache_.Get(drawMeshInstSRVSlot_);
	Assert::Call(
		instanceBinding->parameterType ==
			D3D12_ROOT_PARAMETER_TYPE_SRV,
		"Visible mesh instance SRV must be a root descriptor");
	if (instanceBinding->parameterType !=
		D3D12_ROOT_PARAMETER_TYPE_SRV) {
		return;
	}

	// SV_InstanceIDはStartInstanceLocationを含まないため、
	// LODごとにStructuredBufferの先頭GPUアドレスを切り替える
	const D3D12_GPU_VIRTUAL_ADDRESS visibleBaseAddress =
		prepared.resources->GetVisibleInstanceMeshGPUAddress();
	const uint64_t lodInstanceBytes =
		static_cast<uint64_t>(prepared.instanceCount) *
		sizeof(MeshInstanceData);
	ID3D12Resource* indirectArgs =
		prepared.resources->GetIndexedIndirectArgsResource();
	for (uint32_t lodIndex = 0;
		lodIndex < kMeshLODCount; ++lodIndex) {

		RootBindingCommand::SetGraphicsSRV(
			context.commandList, instanceBinding,
			visibleBaseAddress +
			lodInstanceBytes * lodIndex);
		context.commandList->ExecuteIndirect(
			commandSignature_.Get(), 1,
			indirectArgs,
			sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) *
			lodIndex, nullptr, 0);
	}
}

bool Engine::VertexMeshDrawPath::BuildIndexedIndirectArgs(const MeshPathDrawContext& context) {

	const RenderDrawContext& drawContext = *context.drawContext;
	const MeshPreparedBatch& prepared = *context.prepared;
	if (!drawContext.assetDatabase || !drawContext.assetLibrary || !drawContext.pipelineCache) {
		return false;
	}

	if (!indirectArgsPipeline_) {

		// ビルトインPipelineはパスではなく.meta GUIDで固定参照する
		indirectArgsPipeline_ = BuiltinAssets::Pipelines::BuildIndexedIndirectArgs;
	}

	const PipelineState* pipelineState = drawContext.pipelineCache->GetORCreate(
		context.graphicsCore->GetDXObject(), *drawContext.assetLibrary,
		indirectArgsPipeline_, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState) {
		return false;
	}

	DxCommand* dxCommand = context.graphicsCore->GetDXObject().GetDxCommand();
	ID3D12Resource* indirectArgs = prepared.resources->GetIndexedIndirectArgsResource();
	ID3D12Resource* visibleInstances = prepared.resources->GetVisibleInstanceMeshResource();
	// ComputeがIndirectArgsと可視インスタンス配列を書き込める状態にする
	dxCommand->TransitionBarriers(indirectArgs, prepared.resources->GetIndexedIndirectArgsState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetIndexedIndirectArgsState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	dxCommand->TransitionBarriers(visibleInstances, prepared.resources->GetVisibleInstanceMeshState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetVisibleInstanceMeshState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	context.commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	context.commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// 固定Draw情報、ビュー、カリング設定、入力インスタンスをComputeへ渡す
	indirectArgsBindCache_.Sync(*pipelineState);
	if (indirectArgsBindCache_.Has(indirectArgsCBVSlot_)) {
		RootBindingCommand::SetComputeCBV(context.commandList, indirectArgsBindCache_.Get(indirectArgsCBVSlot_),
			prepared.resources->GetIndirectArgsConstantsGPUAddress());
	}
	if (indirectArgsBindCache_.Has(iaViewCBVSlot_)) {
		RootBindingCommand::SetComputeCBV(context.commandList, indirectArgsBindCache_.Get(iaViewCBVSlot_),
			prepared.resources->GetViewGPUAddress(drawContext.view->kind));
	}
	if (indirectArgsBindCache_.Has(iaDrawCBVSlot_)) {
		RootBindingCommand::SetComputeCBV(context.commandList, indirectArgsBindCache_.Get(iaDrawCBVSlot_),
			prepared.resources->GetDrawGPUAddress());
	}
	if (indirectArgsBindCache_.Has(iaMeshInstSRVSlot_) && prepared.resources->GetInstanceMeshGPUAddress() != 0) {
		RootBindingCommand::SetComputeSRV(context.commandList, indirectArgsBindCache_.Get(iaMeshInstSRVSlot_),
			prepared.resources->GetInstanceMeshGPUAddress(), {});
	}
	if (indirectArgsBindCache_.Has(iaSubMeshSRVSlot_) && prepared.resources->GetSubMeshGPUAddress() != 0) {
		RootBindingCommand::SetComputeSRV(context.commandList, indirectArgsBindCache_.Get(iaSubMeshSRVSlot_),
			prepared.resources->GetSubMeshGPUAddress(), {});
	}
	if (indirectArgsBindCache_.Has(occlusionDepthSRVSlot_) &&
		drawContext.bufferRegistry) {

		const RegisteredRenderBuffer* depthPyramid =
			drawContext.bufferRegistry->Find(
				std::string(DepthPyramidTexture::kBindingName));
		if (depthPyramid && depthPyramid->srvGPUHandle.ptr != 0) {
			RootBindingCommand::SetComputeSRV(
				context.commandList,
				indirectArgsBindCache_.Get(
					occlusionDepthSRVSlot_),
				0, depthPyramid->srvGPUHandle);
		} else {
			const GPUTextureResource* fallback =
				context.graphicsCore->GetBuiltinTextureLibrary()
				.GetWhiteTexture();
			if (fallback) {
				RootBindingCommand::SetComputeSRV(
					context.commandList,
					indirectArgsBindCache_.Get(
						occlusionDepthSRVSlot_),
					0, fallback->gpuHandle);
			}
		}
	} else if (indirectArgsBindCache_.Has(
		occlusionDepthSRVSlot_)) {

		const GPUTextureResource* fallback =
			context.graphicsCore->GetBuiltinTextureLibrary()
			.GetWhiteTexture();
		if (fallback) {
			RootBindingCommand::SetComputeSRV(
				context.commandList,
				indirectArgsBindCache_.Get(
					occlusionDepthSRVSlot_),
				0, fallback->gpuHandle);
		}
	}
	if (indirectArgsBindCache_.Has(visibleInstUAVSlot_)) {
		RootBindingCommand::SetComputeUAV(context.commandList, indirectArgsBindCache_.Get(visibleInstUAVSlot_),
			prepared.resources->GetVisibleInstanceMeshGPUAddress(),
			prepared.resources->GetVisibleInstanceMeshUAVHandle());
	}
	if (indirectArgsBindCache_.Has(idxIndirectArgsUAVSlot_) && prepared.resources->GetIndexedIndirectArgsGPUAddress() != 0) {
		RootBindingCommand::SetComputeUAV(context.commandList, indirectArgsBindCache_.Get(idxIndirectArgsUAVSlot_),
			prepared.resources->GetIndexedIndirectArgsGPUAddress(), {});
	}
	// 1バッチ分のIndirectArgsを1グループで生成する
	context.commandList->Dispatch(1, 1, 1);

	// 書き込み完了後、IndirectArgsはExecuteIndirect用、可視インスタンスはSRV用へ戻す
	dxCommand->UAVBarrier(indirectArgs);
	dxCommand->UAVBarrier(visibleInstances);
	dxCommand->TransitionBarriers(indirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	prepared.resources->SetIndexedIndirectArgsState(D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	D3D12_RESOURCE_STATES visibleReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	dxCommand->TransitionBarriers(visibleInstances, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, visibleReadState);
	prepared.resources->SetVisibleInstanceMeshState(visibleReadState);
	return true;
}
