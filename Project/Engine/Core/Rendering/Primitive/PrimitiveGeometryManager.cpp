#include "PrimitiveGeometryManager.h"

//============================================================================
//	include
//============================================================================
#include "PrimitiveGPUBuilder.h"
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
			it->second.vertexBuffer.Release();
			it->second.indexSRV.Release();
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
	RaytracingBLASGeometryInput geometryInput{};
	geometryInput.vertexAddress =
		geometry.vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress() +
		offsetof(MeshVertex, position);
	geometryInput.vertexStride = sizeof(MeshVertex);
	geometryInput.vertexCount = geometry.vertexCount;
	geometryInput.indexAddress =
		geometry.indexBuffer.GetResource()->GetGPUVirtualAddress();
	geometryInput.indexFormat = geometry.indexBuffer.GetFormat();
	geometryInput.indexCount = geometry.indexCount;

	RaytracingBLASInput input{};
	input.geometries = std::span(&geometryInput, 1);
	input.allowUpdate = false;

	geometry.blas.SetRetirementQueue(srvDescriptor_->GetRetirementQueue());
	geometry.blas.Build(device, commandList, input);
	geometry.blasBuilt = geometry.blas.IsBuilt();
	return geometry.blasBuilt;
}

void Engine::PrimitiveGeometryManager::Clear() {

	for (auto& pair : geometries_) {
		pair.second.vertexBuffer.Release();
		pair.second.indexSRV.Release();
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
	return PrimitiveGPUBuilder::Create(graphicsCore, srvDescriptor_, mesh, geometry);
}
