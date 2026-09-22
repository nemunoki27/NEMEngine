#include "MeshSkinningBufferSet.h"

void Engine::MeshSkinningBufferSet::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

	skinningPalette.Init(device, srvDescriptor);
	skinnedVertices.Init(device, srvDescriptor);
	// MeshShader経路のため、スキニング結果も圧縮頂点として保持する
	skinnedPackedVertices.Init(device, srvDescriptor);
	skinningConstants.Init(device);

	skinningPalette.EnsureCapacity(256);
	skinnedVertices.EnsureCapacity(256);
	skinnedPackedVertices.EnsureCapacity(256);
}

uint32_t Engine::MeshSkinningBufferSet::EnsureVertexCapacity(uint32_t count) {

	uint32_t changed = 0;
	const uint32_t prevCapacity = skinnedVertices.GetCapacity();
	skinnedVertices.EnsureCapacity(count);
	const uint32_t prevPackedCapacity = skinnedPackedVertices.GetCapacity();
	// 通常頂点と圧縮頂点で別リソースなので、容量変更も個別に見る
	skinnedPackedVertices.EnsureCapacity(count);
	// スキニング頂点バッファの容量が変わった場合は、リソース状態をリセットする
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
