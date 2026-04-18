#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Asset/RenderPipelineAsset.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/GPUBuffer/DxConstBuffer.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/MathLib/Color.h>

namespace Engine {

	// front
	class MeshBatchResources;

	//============================================================================
	//	MeshRenderBackendTypes class
	//============================================================================

	// メッシュ描画のために準備されたバッチ
	struct MeshPreparedBatch {

		std::vector<const RenderItem*> items{};

		AssetID batchMesh{};

		MeshBatchResources* resources = nullptr;
		const MeshGPUResource* gpuMesh = nullptr;

		const PipelineVariantDesc* variant = nullptr;
		const PipelineState* pipelineState = nullptr;

		uint32_t instanceCount = 0;
	};
	// メッシュ描画のセットアップのためのコンテキスト
	struct MeshPathSetupContext {

		ID3D12GraphicsCommandList6* commandList = nullptr;
		const RenderDrawContext* drawContext = nullptr;
		const MeshPreparedBatch* prepared = nullptr;
	};
	// メッシュ描画の実行のためのコンテキスト
	struct MeshPathDrawContext {

		GraphicsCore* graphicsCore = nullptr;
		ID3D12GraphicsCommandList6* commandList = nullptr;
		const RenderDrawContext* drawContext = nullptr;
		const MeshPreparedBatch* prepared = nullptr;

		FrameBatchResourcePool<DxConstBuffer<SubMeshConstants>>* subMeshCBPool = nullptr;
		std::vector<GraphicsBindItem>* subMeshBindScratch = nullptr;
	};
}