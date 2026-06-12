#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Foundation/Math/Color.h>

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
	};
}