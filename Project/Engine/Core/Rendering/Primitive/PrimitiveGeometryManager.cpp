#include "PrimitiveGeometryManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingStructures.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>

// c++
#include <cstddef>

//============================================================================
//	PrimitiveGeometryManager classMethods
//============================================================================
void Engine::PrimitiveGeometryManager::Init(GraphicsCore& graphicsCore) {

	srvDescriptor_ = &graphicsCore.GetSRVDescriptor();
}

void Engine::PrimitiveGeometryManager::BeginFrame() {

	++frameIndex_;

	// 猶予フレームを超えて未使用のジオメトリを破棄する
	for (auto it = geometries_.begin(); it != geometries_.end();) {

		if (it->second.lastUsedFrame + kEvictionFrames < frameIndex_) {
			it->second.vertexBuffer.Release(srvDescriptor_);
			it->second.indexSRV.Release(srvDescriptor_);
			it = geometries_.erase(it);
		} else {
			++it;
		}
	}
}

Engine::PrimitiveGeometry* Engine::PrimitiveGeometryManager::GetOrCreate(GraphicsCore& graphicsCore,
	uint64_t hash, const PrimitiveRendererComponent& renderer) {

	if (const auto it = geometries_.find(hash); it != geometries_.end()) {
		it->second.lastUsedFrame = frameIndex_;
		return &it->second;
	}

	PrimitiveGeometry geometry{};
	if (!BuildGeometry(graphicsCore, renderer, geometry)) {
		return nullptr;
	}
	geometry.lastUsedFrame = frameIndex_;

	auto [it, inserted] = geometries_.emplace(hash, std::move(geometry));
	return &it->second;
}

Engine::PrimitiveGeometry* Engine::PrimitiveGeometryManager::Find(uint64_t hash) {

	const auto it = geometries_.find(hash);
	return it != geometries_.end() ? &it->second : nullptr;
}

bool Engine::PrimitiveGeometryManager::EnsureBLAS(ID3D12Device8* device,
	ID3D12GraphicsCommandList6* commandList, PrimitiveGeometry& geometry) {

	if (geometry.blasBuilt && geometry.blas.IsBuilt()) {
		return true;
	}
	if (!geometry.vertexBuffer.buffer || !geometry.indexBuffer.IsCreatedResource() || geometry.indexCount == 0) {
		return false;
	}

	// 描画と同じ頂点/インデックスからBLASを組む、positionは先頭なのでoffsetは0
	RaytracingBLASInput input{};
	input.meshResource = nullptr;
	input.customVertexAddress = geometry.vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress() + offsetof(MeshVertex, position);
	input.customVertexStride = sizeof(MeshVertex);
	input.customVertexCount = geometry.vertexCount;
	input.customVertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	input.customIndexAddress = geometry.indexBuffer.GetResource()->GetGPUVirtualAddress();
	input.customIndexFormat = geometry.indexBuffer.GetFormat();
	input.indexCount = geometry.indexCount;
	input.allowUpdate = false;

	geometry.blas.Build(device, commandList, input);
	geometry.blasBuilt = geometry.blas.IsBuilt();
	return geometry.blasBuilt;
}

void Engine::PrimitiveGeometryManager::Clear() {

	for (auto& pair : geometries_) {
		pair.second.vertexBuffer.Release(srvDescriptor_);
		pair.second.indexSRV.Release(srvDescriptor_);
	}
	geometries_.clear();
	frameIndex_ = 0;
}

bool Engine::PrimitiveGeometryManager::BuildGeometry(GraphicsCore& graphicsCore,
	const PrimitiveRendererComponent& renderer, PrimitiveGeometry& geometry) {

	if (!srvDescriptor_) {
		return false;
	}

	PrimitiveMeshData mesh;
	PrimitiveMeshGenerator::Generate(renderer, mesh);
	if (mesh.vertices.empty() || mesh.indices.empty()) {
		return false;
	}

	// 中立形式からMeshVertexへ詰め替える、描画とレイトレで同じ形式を共用する
	std::vector<MeshVertex> vertices;
	vertices.reserve(mesh.vertices.size());
	for (const PrimitiveMeshVertex& source : mesh.vertices) {

		MeshVertex vertex{};
		vertex.normal = source.normal;
		vertex.tangent = source.tangent;
		vertex.tangentSign = 1.0f;
		vertex.uv = source.texcoord;
		vertex.position = Vector4(source.position.x, source.position.y, source.position.z, 1.0f);
		vertices.emplace_back(vertex);
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	BufferUploadService& uploadService = graphicsCore.GetBufferUploadService();

	// 頂点はSRVで描画、BLASでも同じ本体を読む
	geometry.vertexBuffer.buffer = std::make_unique<DxImmutableStructuredBuffer<MeshVertex>>();
	geometry.vertexBuffer.buffer->Create(device, uploadService, std::span<const MeshVertex>(vertices.data(), vertices.size()));
	const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = geometry.vertexBuffer.buffer->GetSRVDesc();
	srvDescriptor_->CreateSRV(geometry.vertexBuffer.srvIndex, geometry.vertexBuffer.buffer->GetResource(), srvDesc);
	geometry.vertexBuffer.srvGPUHandle = srvDescriptor_->GetGPUHandle(geometry.vertexBuffer.srvIndex);

	// インデックスは描画とBLASでインデックスバッファ、レイトレのヒットシェーダーではSRVで読む
	geometry.indexBuffer.Create(device, uploadService,
		std::span<const uint32_t>(mesh.indices.data(), mesh.indices.size()),
		DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_GENERIC_READ);

	geometry.indexSRV.buffer = std::make_unique<DxImmutableStructuredBuffer<uint32_t>>();
	geometry.indexSRV.buffer->Create(device, uploadService, std::span<const uint32_t>(mesh.indices.data(), mesh.indices.size()));
	const D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = geometry.indexSRV.buffer->GetSRVDesc();
	srvDescriptor_->CreateSRV(geometry.indexSRV.srvIndex, geometry.indexSRV.buffer->GetResource(), indexSrvDesc);
	geometry.indexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(geometry.indexSRV.srvIndex);
	uploadService.SubmitBatch();

	if (!geometry.vertexBuffer.buffer->GetResource() || !geometry.indexBuffer.IsCreatedResource() ||
		!geometry.indexSRV.buffer->GetResource()) {
		geometry.vertexBuffer.Release(srvDescriptor_);
		geometry.indexSRV.Release(srvDescriptor_);
		return false;
	}

	geometry.vertexCount = static_cast<uint32_t>(vertices.size());
	geometry.indexCount = static_cast<uint32_t>(mesh.indices.size());
	return true;
}
