#include "MeshGraphicsBinding.h"

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
Engine::MeshGraphicsBinding::MeshGraphicsBinding() {

	viewCBVSlot_         = sharedBindCache_.AddSlot("ViewConstants",          ShaderBindingKind::CBV);
	drawCBVSlot_         = sharedBindCache_.AddSlot("MeshDrawConstants",      ShaderBindingKind::CBV);
	packedVtxSRVSlot_    = sharedBindCache_.AddSlot("gPackedVertices",        ShaderBindingKind::SRV);
	vtxSubMeshSRVSlot_   = sharedBindCache_.AddSlot("gVertexSubMeshIndices",  ShaderBindingKind::SRV);
	skinnedVtxSRVSlot_   = sharedBindCache_.AddSlot("gSkinnedVertices",       ShaderBindingKind::SRV);
	skinnedPkdVtxSRVSlot_= sharedBindCache_.AddSlot("gSkinnedPackedVertices", ShaderBindingKind::SRV);
	meshInstSRVSlot_     = sharedBindCache_.AddSlot("gMeshInstances",         ShaderBindingKind::SRV);
	subMeshSRVSlot_      = sharedBindCache_.AddSlot("gSubMeshes",             ShaderBindingKind::SRV);
	occlusionDepthSRVSlot_ = sharedBindCache_.AddSlot("gOcclusionDepthPyramid", ShaderBindingKind::SRV);
	outlineSRVSlot_      = sharedBindCache_.AddSlot("gMeshOutlines",          ShaderBindingKind::SRV);
	screenSpaceOutlineMaskCBVSlot_ = sharedBindCache_.AddSlotByRegister(ShaderBindingKind::CBV,
		kScreenSpaceOutlineMaskCBVRegister, kScreenSpaceOutlineMaskCBVSpace);
	shaderGraphTimeCBVSlot_ = sharedBindCache_.AddSlot(
		"ShaderGraphTimeConstants", ShaderBindingKind::CBV);
	materialParamsCBVSlot_ = sharedBindCache_.AddSlot(MaterialParameterCBuffer::kSurface, ShaderBindingKind::CBV);
	subMeshMaterialParamSRVSlot_ = sharedBindCache_.AddSlot(MaterialParameterCBuffer::kMesh, ShaderBindingKind::SRV);

}

void Engine::MeshGraphicsBinding::BeginFrame() {

	constantBufferAllocator_.BeginFrame();
	shaderGraphTimeGPUAddress_ = 0;
	materialParamBinder_.BeginFrame();
}

void Engine::MeshGraphicsBinding::Bind(const RenderDrawContext& context,
	const MeshPreparedBatch& prepared, ID3D12GraphicsCommandList6* commandList) {

	// バッファレジストリ登録済みバッファをまとめてバインドする
	registryAutoBindTable_.Sync(*prepared.pipelineState, *context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(*context.bufferRegistry, commandList);

	// メッシュ固有バインドのスロット解決を更新
	sharedBindCache_.Sync(*prepared.pipelineState);

	if (sharedBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, sharedBindCache_.Get(viewCBVSlot_),
			prepared.resources->GetViewGPUAddress(context.view->kind));
	}
	if (sharedBindCache_.Has(drawCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, sharedBindCache_.Get(drawCBVSlot_),
			prepared.resources->GetDrawGPUAddress());
	}
	if (sharedBindCache_.Has(shaderGraphTimeCBVSlot_) &&
		context.systemContext) {

		if (shaderGraphTimeGPUAddress_ == 0) {
			const SystemContext& systemContext = *context.systemContext;
			const ShaderGraphTimeConstantsGPU constants{
				.time = systemContext.time,
				.deltaTime = systemContext.deltaTime,
				.smoothDeltaTime = systemContext.smoothDeltaTime,
				.unscaledTime = systemContext.unscaledTime,
			};
			const FrameConstantBufferAllocation allocation =
				constantBufferAllocator_.AllocateAndUpload(context.graphicsCore->GetDXObject().GetResourceRetirement(),
			context.graphicsCore->GetDXObject().GetDevice(),
					constants);
			shaderGraphTimeGPUAddress_ = allocation.gpuAddress;
		}
		if (shaderGraphTimeGPUAddress_ != 0) {
			RootBindingCommand::SetGraphicsCBV(
				commandList,
				sharedBindCache_.Get(shaderGraphTimeCBVSlot_),
				shaderGraphTimeGPUAddress_);
		}
	}
	// シェーダーがMaterialParameters cbufferを宣言している場合のみ、reflection駆動でマテリアル値を詰めてバインドする
	// Builtinメッシュシェーダーはこのcbufferを持たずslotが解決されないため、ここは何もしない
	if (sharedBindCache_.Has(materialParamsCBVSlot_) && prepared.material) {

		BackendDrawCommon::BindReflectedMaterialParameters(
			context, materialParamBinder_, *prepared.pipelineState,
			*prepared.material, nullptr, sharedBindCache_,
			materialParamsCBVSlot_, commandList);
	}
	// reflection駆動のサブメッシュ単位マテリアルパラメータを詰めて構造化バッファとしてバインドする
	if (sharedBindCache_.Has(subMeshMaterialParamSRVSlot_) && prepared.material) {

		MaterialParameterLayout subMeshLayout{};
		subMeshLayout.Build(prepared.pipelineState->GetGraphicsReflection(), MaterialParameterCBuffer::kMesh);
		prepared.resources->UploadSubMeshMaterialParams(prepared.material, subMeshLayout, context);
		if (prepared.resources->HasSubMeshMaterialParams()) {
			RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(subMeshMaterialParamSRVSlot_),
				prepared.resources->GetSubMeshMaterialParamGPUAddress(),
				prepared.resources->GetSubMeshMaterialParamGPUHandle());
		}
	}
	// space2のマテリアルテクスチャをreflection駆動でバインドする、Builtinはspace2無で無回帰
	if (prepared.material) {
		BackendDrawCommon::BindMaterialTextures(context, *prepared.pipelineState,
			materialParamBinder_, *prepared.material, commandList);
	}
	if (sharedBindCache_.Has(packedVtxSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(packedVtxSRVSlot_),
			prepared.gpuMesh->packedVertexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->packedVertexSRV.srvGPUHandle);
	}
	if (sharedBindCache_.Has(vtxSubMeshSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(vtxSubMeshSRVSlot_),
			prepared.gpuMesh->vertexSubMeshIndexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->vertexSubMeshIndexSRV.srvGPUHandle);
	}

	// デフォルトは元メッシュ頂点でスキニング済みなら更新後バッファへ差し替える
	D3D12_GPU_VIRTUAL_ADDRESS skinnedVBAddress = prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	D3D12_GPU_DESCRIPTOR_HANDLE skinnedVBHandle = prepared.gpuMesh->vertexSRV.srvGPUHandle;
	D3D12_GPU_VIRTUAL_ADDRESS skinnedPackedVBAddress = prepared.gpuMesh->packedVertexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	D3D12_GPU_DESCRIPTOR_HANDLE skinnedPackedVBHandle = prepared.gpuMesh->packedVertexSRV.srvGPUHandle;
	if (prepared.resources->HasSkinningResources()) {

		skinnedVBAddress = prepared.resources->GetSkinnedVerticesGPUAddress();
		skinnedVBHandle = prepared.resources->GetSkinnedVerticesSRVHandle();
		skinnedPackedVBAddress = prepared.resources->GetSkinnedPackedVerticesGPUAddress();
		skinnedPackedVBHandle = prepared.resources->GetSkinnedPackedVerticesSRVHandle();
	}
	if (sharedBindCache_.Has(skinnedVtxSRVSlot_) && (skinnedVBAddress != 0 || skinnedVBHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(skinnedVtxSRVSlot_),
			skinnedVBAddress, skinnedVBHandle);
	}
	// MeshShaderは圧縮頂点側も読むため、通常頂点と同じタイミングで差し替える
	if (sharedBindCache_.Has(skinnedPkdVtxSRVSlot_) && (skinnedPackedVBAddress != 0 || skinnedPackedVBHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(skinnedPkdVtxSRVSlot_),
			skinnedPackedVBAddress, skinnedPackedVBHandle);
	}
	if (sharedBindCache_.Has(meshInstSRVSlot_) && prepared.resources->GetInstanceMeshGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(meshInstSRVSlot_),
			prepared.resources->GetInstanceMeshGPUAddress(), {});
	}
	if (sharedBindCache_.Has(subMeshSRVSlot_) && prepared.resources->GetSubMeshGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(subMeshSRVSlot_),
			prepared.resources->GetSubMeshGPUAddress(), {});
	}
	if (sharedBindCache_.Has(occlusionDepthSRVSlot_)) {

		D3D12_GPU_DESCRIPTOR_HANDLE depthHandle{};
		if (context.bufferRegistry) {
			const RegisteredRenderBuffer* depthPyramid =
				context.bufferRegistry->Find(
					"gOcclusionDepthPyramid");
			if (depthPyramid) {
				depthHandle = depthPyramid->srvGPUHandle;
			}
		}
		if (depthHandle.ptr == 0) {
			const GPUTextureResource* fallback =
				context.graphicsCore->GetBuiltinTextureLibrary()
				.GetWhiteTexture();
			if (fallback) {
				depthHandle = fallback->gpuHandle;
			}
		}
		if (depthHandle.ptr != 0) {
			RootBindingCommand::SetGraphicsSRV(
				commandList,
				sharedBindCache_.Get(
					occlusionDepthSRVSlot_),
				0, depthHandle);
		}
	}
	// 背面法アウトライン用のインスタンス別GPUデータでOutline系パイプラインだけが参照する
	if (sharedBindCache_.Has(outlineSRVSlot_) && prepared.resources->GetOutlineGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(outlineSRVSlot_),
			prepared.resources->GetOutlineGPUAddress(), {});
	}
	if (sharedBindCache_.Has(screenSpaceOutlineMaskCBVSlot_) &&
		prepared.resources->GetScreenSpaceOutlineMaskGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsCBV(commandList,
			sharedBindCache_.Get(screenSpaceOutlineMaskCBVSlot_),
			prepared.resources->GetScreenSpaceOutlineMaskGPUAddress());
	}
}
