#include "ClothGPUEnvironment.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Formats/VectorFieldData.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameUploadBufferAllocator.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableStructuredBuffer.h>
#include <Engine/Core/World/Systems/Physics/ClothCollision.h>
#include <Engine/Core/World/Systems/Physics/ClothWind.h>

// c++
#include <algorithm>
#include <unordered_map>

namespace Engine {

	// FGAの内容リビジョンごとの格子を束ね、変更時に一度だけDEFAULTヒープへ転送する
	struct ClothGPUVectorAtlas {
		std::vector<std::shared_ptr<const VectorFieldData>> grids;
		DxImmutableStructuredBuffer<Vector3> buffer;
		ID3D12Device* device = nullptr;

		D3D12_GPU_VIRTUAL_ADDRESS GetAddress(GraphicsCore& graphics) {
			auto* current = graphics.GetDXObject().GetDevice();
			if (device == current && buffer.IsCreatedResource()) return buffer.GetResource()->GetGPUVirtualAddress();

			auto& uploader = graphics.GetBufferUploadService();
			DxImmutableStructuredBuffer<Vector3> next;
			if (grids.size() == 1) {
				next.Create(current, uploader, grids.front()->vectors);
			} else {
				size_t count = 0;
				for (const auto& grid : grids) count += grid->vectors.size();
				std::vector<Vector3> joined;
				joined.reserve(count);
				for (const auto& grid : grids) joined.insert(joined.end(), grid->vectors.begin(), grid->vectors.end());
				next.Create(current, uploader, joined);
			}
			uploader.SubmitBatch();
			buffer = std::move(next);
			device = current;
			return buffer.GetResource()->GetGPUVirtualAddress();
		}
	};
}

using namespace Engine;

void ClothGPUEnvironment::Collect(ECSWorld& world, const AssetDatabase* database,
	std::unique_ptr<ParticleFieldAssetCache>& assets, std::shared_ptr<ClothGPUVectorAtlas>& cachedAtlas,
	bool collectFields, bool collectColliders) {

	uploadFrame_ = UINT64_MAX;
	device_ = nullptr;
	fields.clear();
	atlas_.reset();
	colliders.clear();
	if (collectColliders) {
		for (const auto& candidate : ClothCollisionQuery::Collect(world)) {
			const auto& shape = candidate.shape;
			auto& collider = colliders.emplace_back();
			collider.center = shape.center;
			collider.radius = shape.radius;
			std::copy_n(shape.axes, 3, collider.axes);
			collider.halfExtents = shape.halfExtents;
			collider.start = shape.segmentStart;
			collider.end = shape.segmentEnd;
			collider.type = static_cast<uint32_t>(shape.type);
			collider.mask = candidate.typeMask;
		}
	}
	if (!collectFields) return;
	std::unordered_map<const VectorFieldData*, uint32_t> gridOffsets;
	std::vector<std::shared_ptr<const VectorFieldData>> grids;
	uint32_t sampleCount = 0;
	for (const auto& source : ClothWind::CollectWind(world, database, assets)) {
		auto& field = fields.emplace_back();
		field.inverse = source.inverse;
		field.world = source.world;
		field.center = source.center;
		field.axis = source.axis;
		field.mask = source.mask;
		field.strength = source.strength;
		field.falloff = source.falloff;
		field.tightness = source.tightness;
		field.shape = static_cast<uint32_t>(source.shape);
		field.kind = source.vector ? 4u + static_cast<uint32_t>(source.preset) : static_cast<uint32_t>(source.forceType);
		if (!source.grid) continue;
		const auto& grid = *source.grid;
		const auto [it, inserted] = gridOffsets.try_emplace(&grid, sampleCount);
		field.grid[0] = grid.resolution.x;
		field.grid[1] = grid.resolution.y;
		field.grid[2] = grid.resolution.z;
		field.grid[3] = it->second;
		if (inserted) {
			if (grid.vectors.size() > UINT32_MAX - sampleCount) throw std::length_error("ClothのFGAサンプル数が上限を超えています");
			grids.push_back(source.grid);
			sampleCount += static_cast<uint32_t>(grid.vectors.size());
		}
	}
	if (grids.empty()) {
		cachedAtlas.reset();
		return;
	}
	if (!cachedAtlas || cachedAtlas->grids != grids) {
		cachedAtlas = std::make_shared<ClothGPUVectorAtlas>();
		cachedAtlas->grids = std::move(grids);
	}
	atlas_ = cachedAtlas;
}

ClothGPUEnvironment::Addresses ClothGPUEnvironment::GetGPUAddresses(GraphicsCore& graphics, FrameUploadBufferAllocator& uploads) const {

	auto& platform = graphics.GetDXObject();
	const auto frame = GraphicsFrameState::GetFrameSerial();
	auto* device = platform.GetDevice();
	if (uploadFrame_ == frame && device_ == device) return addresses_;

	// 同じワールド更新を処理する全ての布で、同じフレームの転送領域を共有する
	const auto transfer = [&](const auto& values) -> D3D12_GPU_VIRTUAL_ADDRESS {
		if (values.empty()) return 0;
		auto data = reinterpret_cast<const uint8_t*>(values.data());
		return uploads.AllocateAndUploadBytes(platform.GetResourceRetirement(), device,
			{data, values.size() * sizeof(values.front())}).gpuAddress;
	};
	addresses_ = {transfer(fields), atlas_ ? atlas_->GetAddress(graphics) : 0, transfer(colliders)};
	device_ = device;
	uploadFrame_ = frame;
	return addresses_;
}
