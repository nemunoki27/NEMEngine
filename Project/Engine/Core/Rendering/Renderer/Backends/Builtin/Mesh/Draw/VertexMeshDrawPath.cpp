#include "VertexMeshDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Core/D3D12CommandContext.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	VertexMeshDrawPath classMethods
//============================================================================

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

void Engine::VertexMeshDrawPath::Setup(const MeshPathSetupContext& context, [[maybe_unused]] std::vector<GraphicsBindItem>& scratch) {

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
	if (context.subMeshBindScratch) {

		context.subMeshBindScratch->clear();
		// VS側はカリング済みインスタンス配列を通常のgMeshInstancesとして読む
		context.subMeshBindScratch->push_back({
			prepared.resources->GetInstanceMeshBindingName(),
			GraphicsBindValueType::SRV,
			prepared.resources->GetVisibleInstanceMeshGPUAddress(),
			prepared.resources->GetVisibleInstanceMeshSRVHandle()
			});
		GraphicsRootBinder binder{ *prepared.pipelineState };
		binder.Bind(context.commandList, *context.subMeshBindScratch);
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

	dxCommand->SetDescriptorHeaps({ context.graphicsCore->GetSRVDescriptor().GetDescriptorHeap() });

	context.commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	context.commandList->SetPipelineState(pipelineState->GetComputePipeline());

	std::vector<ComputeBindItem> bindItems{};
	bindItems.reserve(10);
	// 固定Draw情報、ビュー、カリング設定、入力インスタンスをComputeへ渡す
	bindItems.push_back({
		prepared.resources->GetIndirectArgsConstantsBindingName(),
		ComputeBindValueType::CBV,
		prepared.resources->GetIndirectArgsConstantsGPUAddress()
		});
	bindItems.push_back({
		prepared.resources->GetViewBindingName(),
		ComputeBindValueType::CBV,
		prepared.resources->GetViewGPUAddress(drawContext.view->kind)
		});
	bindItems.push_back({
		prepared.resources->GetDrawBindingName(),
		ComputeBindValueType::CBV,
		prepared.resources->GetDrawGPUAddress()
		});
	bindItems.push_back({
		prepared.resources->GetInstanceMeshBindingName(),
		ComputeBindValueType::SRV,
		prepared.resources->GetInstanceMeshGPUAddress()
		});
	bindItems.push_back({
		prepared.resources->GetSubMeshBindingName(),
		ComputeBindValueType::SRV,
		prepared.resources->GetSubMeshGPUAddress()
		});
	bindItems.push_back({
		"gVisibleMeshInstances",
		ComputeBindValueType::UAV,
		prepared.resources->GetVisibleInstanceMeshGPUAddress(),
		prepared.resources->GetVisibleInstanceMeshUAVHandle()
		});
	bindItems.push_back({
		"gIndexedIndirectArgs",
		ComputeBindValueType::UAV,
		prepared.resources->GetIndexedIndirectArgsGPUAddress()
	});
	ComputeRootBinder binder{ *pipelineState };
	binder.Bind(context.commandList, bindItems);
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
