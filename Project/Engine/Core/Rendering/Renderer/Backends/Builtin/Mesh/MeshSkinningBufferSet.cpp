#include "MeshSkinningBufferSet.h"

void Engine::MeshSkinningBufferSet::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

	skinningPalette.Init(device, srvDescriptor);
	skinnedVertices.Init(device, srvDescriptor);
	// MeshShader用の圧縮頂点も生成
	skinnedPackedVertices.Init(device, srvDescriptor);
	skinningConstants.Init(srvDescriptor->GetRetirementQueue(), device);

	skinningPalette.EnsureCapacity(256);
	skinnedVertices.EnsureCapacity(256);
	skinnedPackedVertices.EnsureCapacity(256);
}

uint32_t Engine::MeshSkinningBufferSet::EnsureVertexCapacity(uint32_t count) {

	uint32_t changed = 0;
	const uint32_t prevCapacity = skinnedVertices.GetCapacity();
	skinnedVertices.EnsureCapacity(count);
	const uint32_t prevPackedCapacity = skinnedPackedVertices.GetCapacity();
	// 通常頂点と圧縮頂点の容量を個別に更新
	skinnedPackedVertices.EnsureCapacity(count);
	// 再生成した頂点の状態を戻す
	if (prevCapacity != skinnedVertices.GetCapacity()) {

		skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
		++changed;
	}
	if (prevPackedCapacity != skinnedPackedVertices.GetCapacity()) {

		skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
		++changed;
	}

	return changed;
}

void Engine::MeshSkinningBufferSet::Upload(std::span<const WellForGPU> palette, uint32_t vertexCount,
	uint32_t boneCount, uint32_t instanceCount) {

	skinningPalette.Upload(palette);

	MeshSkinningDispatchConstants constants{};
	constants.vertexCount = vertexCount;
	constants.boneCount = boneCount;
	constants.skinnedInstanceCount = instanceCount;
	skinningConstants.Upload(constants);
}
