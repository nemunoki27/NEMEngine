#include "VertexMeshDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Core/D3D12CommandContext.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
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
	visibleInstUAVSlot_      = indirectArgsBindCache_.AddSlot("gVisibleMeshInstances",   ShaderBindingKind::UAV);
	idxIndirectArgsUAVSlot_  = indirectArgsBindCache_.AddSlot("gIndexedIndirectArgs",    ShaderBindingKind::UAV);

	// Draw時: カリング済みインスタンス配列をgMeshInstancesとして再バインドする
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

	EnsureCommandSignature(context.graphicsCore->GetDXObject().GetDevice());
	// Index数は固定、Instance数や可視インスタンス配列はComputeで決定する
	prepared.resources->UpdateIndexedIndirectArgsConstants(prepared.gpuMesh->indexCount);
	if (!BuildIndexedIndirectArgs(context)) {
		return;
	}
	context.commandList->SetPipelineState(prepared.pipelineState->GetGraphicsPipeline(prepared.items.front()->blendMode));

	// VS側はカリング済みインスタンス配列を通常のgMeshInstancesとして読む
	drawBindCache_.Sync(*prepared.pipelineState);
	if (drawBindCache_.Has(drawMeshInstSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, drawBindCache_.Get(drawMeshInstSRVSlot_),
			prepared.resources->GetVisibleInstanceMeshGPUAddress(),
			prepared.resources->GetVisibleInstanceMeshSRVHandle());
	}

	// Computeで作成した引数を使って、マルチメッシュバッチを1回のIndirect Drawで描画する
	context.commandList->ExecuteIndirect(commandSignature_.Get(), 1,
		prepared.resources->GetIndexedIndirectArgsResource(), 0, nullptr, 0);
}

bool Engine::VertexMeshDrawPath::BuildIndexedIndirectArgs(const MeshPathDrawContext& context) {

	const RenderDrawContext& drawContext = *context.drawContext;
	const MeshPreparedBatch& prepared = *context.prepared;
	if (!drawContext.assetDatabase || !drawContext.assetLibrary || !drawContext.pipelineCache) {
		return false;
	}

	if (!indirectArgsPipeline_) {

		indirectArgsPipeline_ = drawContext.assetDatabase->ImportOrGet(
			"Engine/Assets/Pipelines/Builtin/Mesh/buildIndexedIndirectArgs.pipeline.json", AssetType::RenderPipeline);
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
	dxCommand->TransitionBarriers({ indirectArgs }, prepared.resources->GetIndexedIndirectArgsState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetIndexedIndirectArgsState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	dxCommand->TransitionBarriers({ visibleInstances }, prepared.resources->GetVisibleInstanceMeshState(),
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
	dxCommand->TransitionBarriers({ indirectArgs }, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	prepared.resources->SetIndexedIndirectArgsState(D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	D3D12_RESOURCE_STATES visibleReadState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	dxCommand->TransitionBarriers({ visibleInstances }, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, visibleReadState);
	prepared.resources->SetVisibleInstanceMeshState(visibleReadState);
	return true;
}
