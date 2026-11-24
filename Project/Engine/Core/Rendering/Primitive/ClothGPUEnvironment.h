#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <cstdint>
#include <memory>
#include <vector>
// directX
#include <d3d12.h>

namespace Engine {

	class GraphicsCore;
	class FrameUploadBufferAllocator;
	class ECSWorld;
	class AssetDatabase;
	class ParticleFieldAssetCache;
	struct ClothGPUVectorAtlas;

	struct ClothGPUWindField {
		Matrix4x4 inverse, world;
		Vector3 center;
		uint32_t mask;
		Vector3 axis;
		float strength;
		float falloff, tightness;
		uint32_t shape, kind;
		uint32_t grid[4];
	};
	static_assert(sizeof(ClothGPUWindField) == 192);

	struct ClothGPUCollider {
		Vector3 center;
		float radius;
		Vector3 axes[3];
		Vector3 halfExtents;
		Vector3 start;
		uint32_t type;
		Vector3 end;
		uint32_t mask;
	};
	static_assert(sizeof(ClothGPUCollider) == 96);

	struct ClothGPUEnvironment {
		std::vector<ClothGPUWindField> fields;
		std::vector<ClothGPUCollider> colliders;

		struct Addresses {
			D3D12_GPU_VIRTUAL_ADDRESS fields = 0, samples = 0, colliders = 0;
		};

		void Collect(ECSWorld& world, const AssetDatabase* database, std::unique_ptr<ParticleFieldAssetCache>& assets,
			std::shared_ptr<ClothGPUVectorAtlas>& cachedAtlas, bool collectFields, bool collectColliders);
		Addresses GetGPUAddresses(GraphicsCore& graphics, FrameUploadBufferAllocator& uploads) const;

	private:
		std::shared_ptr<ClothGPUVectorAtlas> atlas_;
		mutable Addresses addresses_{};
		mutable ID3D12Device* device_ = nullptr;
		mutable uint64_t uploadFrame_ = UINT64_MAX;
	};
}
