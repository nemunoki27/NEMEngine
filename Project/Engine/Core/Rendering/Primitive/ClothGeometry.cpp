#include "PrimitiveGeometryManager.h"

//============================================================================
//	include
//============================================================================
#include "PrimitiveGPUBuilder.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxStructuredBuffer.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine {

	struct ClothVertexBuffers {

		struct Slot {
			DxStructuredBuffer<MeshVertex> vertices;
			uint32_t descriptor = UINT32_MAX;
			uint64_t revision = 0;
		};

		SRVDescriptor& descriptors;
		std::array<Slot, kGraphicsFrameContextCount> slots;
		std::shared_ptr<const ECSWorldLifetime> lifetime;
		std::vector<MeshVertex> staging;
		uint32_t generation = 0;
		uint32_t index = 0;
		int32_t divideX = 0;
		uint64_t revision = 0;

		explicit ClothVertexBuffers(SRVDescriptor& owner) : descriptors(owner) {}
		~ClothVertexBuffers() {
			for (auto& slot : slots) {
				if (slot.descriptor != UINT32_MAX) descriptors.Retire(slot.descriptor, slot.vertices.GetResource());
			}
		}
	};
}

ID3D12Resource* Engine::PrimitiveGeometry::GetVertexResource() const {

	if (clothVertices) return clothVertices->slots[GraphicsFrameState::GetCurrentIndex()].vertices.GetResource();
	return vertexBuffer.buffer ? vertexBuffer.buffer->GetResource() : nullptr;
}

uint32_t Engine::PrimitiveGeometry::GetVertexDescriptorIndex() const {

	return clothVertices ? clothVertices->slots[GraphicsFrameState::GetCurrentIndex()].descriptor : vertexBuffer.srvIndex;
}

const Engine::ClothRuntime* Engine::FindClothGeometry(const PrimitiveRendererComponent& renderer, const ECSWorld* world, Entity owner) {

	const auto* cloth = world && IsClothPlane(&renderer) ? TryGetCloth(*world, owner) : nullptr;
	return cloth && cloth->settings.enabled && !cloth->runtime.mesh.vertices.empty() ? &cloth->runtime : nullptr;
}

Engine::PrimitiveGeometry* Engine::PrimitiveGeometryManager::Resolve(GraphicsCore& graphicsCore,
	const PrimitiveRendererComponent& renderer, const ECSWorld* world, Entity owner) {

	const auto* cloth = FindClothGeometry(renderer, world, owner);
	if (!cloth) return GetOrCreate(graphicsCore, renderer);
	const auto& mesh = cloth->mesh;
	const auto handle = world->TryGetComponent<ClothComponent>(owner)->handle;
	auto& geometry = clothGeometries_[{world, owner}];
	if (!geometry.clothVertices || geometry.vertexCount != mesh.vertices.size() ||
		geometry.clothVertices->divideX != renderer.plane.divideX || geometry.clothVertices->lifetime != world->GetLifetime() ||
		geometry.clothVertices->generation != handle.generation || geometry.clothVertices->index != handle.index) {

		PrimitiveGeometry replacement;
		replacement.clothVertices = std::make_shared<ClothVertexBuffers>(*srvDescriptor_);
		replacement.clothVertices->lifetime = world->GetLifetime();
		replacement.clothVertices->generation = handle.generation;
		replacement.clothVertices->index = handle.index;
		replacement.clothVertices->divideX = renderer.plane.divideX;
		replacement.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
		replacement.indexCount = static_cast<uint32_t>(mesh.indices.size());
		PrimitiveGPUBuilder::CreateIndices(graphicsCore, *srvDescriptor_, mesh, replacement);
		geometry = std::move(replacement);
	}

	auto& buffers = *geometry.clothVertices;
	if (buffers.staging.empty() || buffers.revision != cloth->revision) {
		PrimitiveGPUBuilder::ConvertVertices(mesh, buffers.staging, geometry.planarUVBounds);
		geometry.boundsCenter = cloth->boundsCenter;
		geometry.boundsRadius = cloth->boundsRadius;
		buffers.revision = cloth->revision;
		geometry.blasBuilt = false;
	}

	// GPUが読み終えたフレームの頂点だけを書き換える
	auto& slot = buffers.slots[GraphicsFrameState::GetCurrentIndex()];
	const bool create = slot.descriptor == UINT32_MAX;
	if (create) {
		slot.vertices.CreateSRVBuffer(graphicsCore.GetDXObject().GetDevice(), geometry.vertexCount);
		srvDescriptor_->CreateSRV(slot.descriptor, slot.vertices.GetResource(), slot.vertices.GetSRVDesc(geometry.vertexCount));
	}
	if (create || slot.revision != buffers.revision) {
		slot.vertices.TransferData(buffers.staging);
		slot.revision = buffers.revision;
	}
	geometry.lastUsedFrame = frameIndex_;
	return &geometry;
}
