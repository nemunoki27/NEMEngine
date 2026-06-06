#include "MeshShaderDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

//============================================================================
//	MeshShaderDrawPath classMethods
//============================================================================
Engine::MeshShaderDrawPath::MeshShaderDrawPath() {

	// メッシュレット関連SRVスロットを初期化時に登録する
	indicesSRVSlot_         = meshletBindCache_.AddSlot("gIndices",                    ShaderBindingKind::SRV);
	meshletsSRVSlot_        = meshletBindCache_.AddSlot("gMeshlets",                   ShaderBindingKind::SRV);
	meshletBoundsSRVSlot_   = meshletBindCache_.AddSlot("gMeshletBounds",              ShaderBindingKind::SRV);
	meshletVtxIdxSRVSlot_   = meshletBindCache_.AddSlot("gMeshletVertexIndices",       ShaderBindingKind::SRV);
	meshletPrimIdxSRVSlot_  = meshletBindCache_.AddSlot("gMeshletPrimitiveIndices",    ShaderBindingKind::SRV);
	pkdMeshletVtxIdxSRVSlot_= meshletBindCache_.AddSlot("gPackedMeshletVertexIndices", ShaderBindingKind::SRV);
}

bool Engine::MeshShaderDrawPath::Supports(const PipelineVariantDesc& variant) const {

	return variant.kind == PipelineVariantKind::GraphicsMesh;
}

void Engine::MeshShaderDrawPath::Setup(const MeshPathSetupContext& context) {

	const MeshPreparedBatch& prepared = *context.prepared;

	// パイプラインが変わった時だけスロットを再解決する
	meshletBindCache_.Sync(*prepared.pipelineState);

	// IndexBuffer（indexSRV.buffer は常に存在する前提）
	if (meshletBindCache_.Has(indicesSRVSlot_) && prepared.gpuMesh->indexSRV.buffer) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, meshletBindCache_.Get(indicesSRVSlot_),
			prepared.gpuMesh->indexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->indexSRV.srvGPUHandle);
	}
	// MeshletBuffers: AS/MS側で必要なメッシュレット情報を直接SRVとして渡す
	if (meshletBindCache_.Has(meshletsSRVSlot_) &&
		(prepared.gpuMesh->meshletDrawSRV.buffer || prepared.gpuMesh->meshletDrawSRV.srvGPUHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, meshletBindCache_.Get(meshletsSRVSlot_),
			prepared.gpuMesh->meshletDrawSRV.buffer ? prepared.gpuMesh->meshletDrawSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
			prepared.gpuMesh->meshletDrawSRV.srvGPUHandle);
	}
	if (meshletBindCache_.Has(meshletBoundsSRVSlot_) &&
		(prepared.gpuMesh->meshletBoundsSRV.buffer || prepared.gpuMesh->meshletBoundsSRV.srvGPUHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, meshletBindCache_.Get(meshletBoundsSRVSlot_),
			prepared.gpuMesh->meshletBoundsSRV.buffer ? prepared.gpuMesh->meshletBoundsSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
			prepared.gpuMesh->meshletBoundsSRV.srvGPUHandle);
	}
	// 通常頂点用Indexと圧縮頂点用Indexを両方渡し、シェーダ側のフラグで選択する
	if (meshletBindCache_.Has(meshletVtxIdxSRVSlot_) &&
		(prepared.gpuMesh->meshletVertexIndexSRV.buffer || prepared.gpuMesh->meshletVertexIndexSRV.srvGPUHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, meshletBindCache_.Get(meshletVtxIdxSRVSlot_),
			prepared.gpuMesh->meshletVertexIndexSRV.buffer ? prepared.gpuMesh->meshletVertexIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
			prepared.gpuMesh->meshletVertexIndexSRV.srvGPUHandle);
	}
	if (meshletBindCache_.Has(meshletPrimIdxSRVSlot_) &&
		(prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer || prepared.gpuMesh->meshletPrimitiveIndexSRV.srvGPUHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, meshletBindCache_.Get(meshletPrimIdxSRVSlot_),
			prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer ? prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
			prepared.gpuMesh->meshletPrimitiveIndexSRV.srvGPUHandle);
	}
	if (meshletBindCache_.Has(pkdMeshletVtxIdxSRVSlot_) &&
		(prepared.gpuMesh->packedMeshletVertexIndexSRV.buffer || prepared.gpuMesh->packedMeshletVertexIndexSRV.srvGPUHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(context.commandList, meshletBindCache_.Get(pkdMeshletVtxIdxSRVSlot_),
			prepared.gpuMesh->packedMeshletVertexIndexSRV.buffer ? prepared.gpuMesh->packedMeshletVertexIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
			prepared.gpuMesh->packedMeshletVertexIndexSRV.srvGPUHandle);
	}
}

void Engine::MeshShaderDrawPath::Draw(const MeshPathDrawContext& context) {

	const auto& prepared = *context.prepared;

	const uint32_t meshletCount = prepared.gpuMesh->meshletCount;
	// MeshletやInstanceがない場合はDispatchMeshしない
	if (meshletCount == 0 || prepared.instanceCount == 0) {
		return;
	}

	Assert::Call(meshletCount <= 65535, "meshletCount > 65535 is not supported yet");
	Assert::Call(prepared.instanceCount <= 65535, "instanceCount > 65535 is not supported yet");

	// X方向は32メッシュレット単位、Y方向はインスタンス単位でASを起動する
	const uint32_t meshletGroupCount = DxUtils::RoundUp(meshletCount, 32);
	context.commandList->DispatchMesh(meshletGroupCount, prepared.instanceCount, 1);
}
