#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshSkinningSharedTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>

namespace Engine {

	//============================================================================
	//	MeshSkinningBufferSet class
	//	Skinningの入出力Bufferを所有する
	//============================================================================
	class MeshSkinningBufferSet {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Paletteと頂点の転送先を生成する
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor);
		// 頂点容量を更新して再生成したBuffer数を返す
		uint32_t EnsureVertexCapacity(uint32_t count);
		// PaletteとDispatch定数を転送する
		void Upload(std::span<const WellForGPU> palette, uint32_t vertexCount, uint32_t boneCount, uint32_t instanceCount);
	private:
		//========================================================================
		//	private Methods
		//========================================================================
		friend class MeshBatchResources;

		StructuredInstanceBuffer<WellForGPU> skinningPalette{ "gSkinningPalette" };
		StructuredRWBuffer<MeshVertex> skinnedVertices{ "gSkinnedVertices" };
		// MeshShader用に法線をOct圧縮したスキニング結果を保持する
		StructuredRWBuffer<MeshPackedVertex> skinnedPackedVertices{ "gSkinnedPackedVertices" };
		ViewConstantBuffer<MeshSkinningDispatchConstants> skinningConstants{ "SkinningConstants" };

		D3D12_RESOURCE_STATES skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
		D3D12_RESOURCE_STATES skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;

	};
}
