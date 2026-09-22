#include "MeshSkinningDispatcher.h"

//============================================================================
//	include
//============================================================================
#include "MeshRenderBackendTypes.h"
#include "MeshBatchResources.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/VertexMeshDrawPath.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/MeshShaderDrawPath.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cstdint>

//============================================================================
//	MeshRenderBackend classMethods
//============================================================================
Engine::MeshSkinningDispatcher::MeshSkinningDispatcher() {

	skinConstCBVSlot_     = skinningBindCache_.AddSlot("SkinningConstants",      ShaderBindingKind::CBV);
	inputVtxSRVSlot_      = skinningBindCache_.AddSlot("gInputVertices",         ShaderBindingKind::SRV);
	vtxInflSRVSlot_       = skinningBindCache_.AddSlot("gVertexInfluences",      ShaderBindingKind::SRV);
	skinPaletteSRVSlot_   = skinningBindCache_.AddSlot("gSkinningPalette",       ShaderBindingKind::SRV);
	skinnedVtxUAVSlot_    = skinningBindCache_.AddSlot("gSkinnedVertices",       ShaderBindingKind::UAV);
	skinnedPkdVtxUAVSlot_ = skinningBindCache_.AddSlot("gSkinnedPackedVertices", ShaderBindingKind::UAV);
}

void Engine::MeshSkinningDispatcher::Dispatch(const RenderDrawContext& context, const MeshPreparedBatch& prepared) {

	// スキニングしないメッシュの場合は何もしない
	if (!prepared.gpuMesh->isSkinned) {
		return;
	}
	// スキニングを行うインスタンスがない場合
	// スキニングに必要なリソースがない場合
	// すでにスキニング処理をディスパッチしている場合
	if (prepared.resources->GetSkinnedInstanceCount() == 0||
		!prepared.resources->HasSkinningResources()||
		prepared.resources->IsSkinningDispatched()) {
		return;
	}

	GraphicsCore& graphicsCore = *context.graphicsCore;

	// スキニングに必要な入力が揃っていない場合は何もしない
	if (!prepared.gpuMesh->vertexSRV.buffer || !prepared.gpuMesh->skinInfluenceSRV.buffer) {
		return;
	}

	// スキニングパイプラインアセットを読み込む
	if (!skinningPipeline_) {

		// ビルトインPipelineはパスではなく.meta GUIDで固定参照する
		skinningPipeline_ = BuiltinAssets::Pipelines::Skinning;
	}

	// スキニングパイプラインの取得
	const PipelineState* pipelineState = context.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*context.assetLibrary, skinningPipeline_, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState) {
		return;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();

	// スキニング結果の出力先リソースを取得
	ID3D12Resource* output = prepared.resources->GetSkinnedVerticesResource();
	ID3D12Resource* packedOutput = prepared.resources->GetSkinnedPackedVerticesResource();
	if (!output || !packedOutput) {
		return;
	}

	// UAV書き込みへ遷移
	dxCommand->TransitionBarriers(output, prepared.resources->GetSkinnedVertexState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetSkinnedVertexState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	dxCommand->TransitionBarriers(packedOutput, prepared.resources->GetSkinnedPackedVertexState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetSkinnedPackedVertexState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// パイプラインを設定
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// バッファバインドはパイプラインが変わった時だけ再解決する
	skinningBindCache_.Sync(*pipelineState);
	if (skinningBindCache_.Has(skinConstCBVSlot_)) {
		RootBindingCommand::SetComputeCBV(commandList, skinningBindCache_.Get(skinConstCBVSlot_),
			prepared.resources->GetSkinningConstantsGPUAddress());
	}
	if (skinningBindCache_.Has(inputVtxSRVSlot_)) {
		RootBindingCommand::SetComputeSRV(commandList, skinningBindCache_.Get(inputVtxSRVSlot_),
			prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->vertexSRV.srvGPUHandle);
	}
	if (skinningBindCache_.Has(vtxInflSRVSlot_)) {
		RootBindingCommand::SetComputeSRV(commandList, skinningBindCache_.Get(vtxInflSRVSlot_),
			prepared.gpuMesh->skinInfluenceSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->skinInfluenceSRV.srvGPUHandle);
	}
	if (skinningBindCache_.Has(skinPaletteSRVSlot_) && prepared.resources->GetSkinningPaletteGPUAddress() != 0) {
		RootBindingCommand::SetComputeSRV(commandList, skinningBindCache_.Get(skinPaletteSRVSlot_),
			prepared.resources->GetSkinningPaletteGPUAddress(), {});
	}
	if (skinningBindCache_.Has(skinnedVtxUAVSlot_)) {
		RootBindingCommand::SetComputeUAV(commandList, skinningBindCache_.Get(skinnedVtxUAVSlot_),
			prepared.resources->GetSkinnedVerticesGPUAddress(),
			prepared.resources->GetSkinnedVerticesUAVHandle());
	}
	if (skinningBindCache_.Has(skinnedPkdVtxUAVSlot_)) {
		RootBindingCommand::SetComputeUAV(commandList, skinningBindCache_.Get(skinnedPkdVtxUAVSlot_),
			prepared.resources->GetSkinnedPackedVerticesGPUAddress(),
			prepared.resources->GetSkinnedPackedVerticesUAVHandle());
	}

	// スキニング処理をディスパッチ
	// Xは頂点数、Yはスキニング対象インスタンス数
	commandList->Dispatch(DxUtils::RoundUp(prepared.gpuMesh->vertexCount, 256),
		prepared.resources->GetSkinnedInstanceCount(), 1);
	FrameProfiler::GetInstance().AddSkinningDispatch(
		prepared.resources->GetSkinnedInstanceCount());

	// UAVバリアを挿入して、スキニング結果の書き込み完了を保証する
	dxCommand->UAVBarrier(output);
	dxCommand->UAVBarrier(packedOutput);

	D3D12_RESOURCE_STATES readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// スキニング結果をシェーダーリソースとして使用できるように遷移
	dxCommand->TransitionBarriers(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, readState);
	dxCommand->TransitionBarriers(packedOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, readState);

	// スキニング結果のリソース状態を更新して、スキニング処理をディスパッチしたことをセットする
	prepared.resources->SetSkinnedVertexState(readState);
	prepared.resources->SetSkinnedPackedVertexState(readState);
	prepared.resources->MarkSkinningDispatched();
}
