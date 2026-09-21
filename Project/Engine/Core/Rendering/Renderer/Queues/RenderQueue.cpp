#include "RenderQueue.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

//============================================================================
//	RenderQueue classMethods
//============================================================================
void Engine::RenderSceneBatch::Add(RenderItem&& item) {

	item.previousWorldMatrix = item.worldMatrix;
	items_.emplace_back(std::move(item));
}

void Engine::RenderSceneBatch::Clear() {

	items_.clear();
	entityItemLookup_.clear();
	transformChanges_.clear();
	payloadArena_.Clear();
	sourceWorld_ = nullptr;
	sourceRenderRevision_ = 0;
	sourceMaterialRevision_ = 0;
	sourceTransformRevision_ = 0;
	completeTransformChanges_ = false;
}

void Engine::RenderSceneBatch::Reserve(uint32_t itemCount, uint32_t payloadByteCount) {

	if (items_.capacity() < itemCount) {

		items_.reserve(itemCount);
	}
	payloadArena_.Reserve(payloadByteCount);
}

void Engine::RenderSceneBatch::Sort() {

	auto less = [](const RenderItem& itemA, const RenderItem& itemB) {

		// 描画フェーズ比較
		if (itemA.renderPhase != itemB.renderPhase) {
			return itemA.renderPhase < itemB.renderPhase;
		}
		// ソートレイヤー比較
		if (itemA.sortingLayer != itemB.sortingLayer) {
			return itemA.sortingLayer < itemB.sortingLayer;
		}
		// ソート順比較
		if (itemA.sortingOrder != itemB.sortingOrder) {
			return itemA.sortingOrder < itemB.sortingOrder;
		}
		// Canvas配下は異なるマテリアルでもヒエラルキーの重なり順を維持する
		if (itemA.orderedUI || itemB.orderedUI) {
			if (itemA.orderedUI != itemB.orderedUI) {
				return !itemA.orderedUI;
			}
			if (itemA.hierarchyOrder != itemB.hierarchyOrder) {
				return itemA.hierarchyOrder < itemB.hierarchyOrder;
			}
		}
		// マテリアル比較
		if (itemA.material != itemB.material) {
			return itemA.material < itemB.material;
		}
		// 描画ID比較
		if (itemA.backendID != itemB.backendID) {
			return itemA.backendID < itemB.backendID;
		}
		// ブレンドモード比較
		if (itemA.blendMode != itemB.blendMode) {
			return itemA.blendMode < itemB.blendMode;
		}
		// バッチキー比較
		if (itemA.batchKey != itemB.batchKey) {
			return itemA.batchKey < itemB.batchKey;
		}

		// 描画順が変わらないならエンティティIDでソートする
		if (itemA.entity.index != itemB.entity.index) {
			return itemA.entity.index < itemB.entity.index;
		}
		return itemA.entity.generation < itemB.entity.generation;
		};
	std::stable_sort(items_.begin(), items_.end(), less);
	RebuildEntityLookup();
}

void Engine::RenderSceneBatch::SetSource(const ECSWorld* world,
	uint64_t renderRevision, uint64_t transformRevision) {

	sourceWorld_ = world;
	sourceRenderRevision_ = renderRevision;
	sourceTransformRevision_ = transformRevision;
	transformChanges_.clear();
	completeTransformChanges_ = false;
	RebuildEntityLookup();
	++contentRevision_;
	if (contentRevision_ == 0) {
		contentRevision_ = 1;
	}
}

void Engine::RenderSceneBatch::SetMaterialSource(uint64_t revision) {

	if (sourceMaterialRevision_ == revision) { return; }
	sourceMaterialRevision_ = revision;
	++contentRevision_;
	if (contentRevision_ == 0) { contentRevision_ = 1; }
}

void Engine::RenderSceneBatch::SetTransformSource(
	uint64_t transformRevision, bool completeChanges) {

	sourceTransformRevision_ = transformRevision;
	completeTransformChanges_ = completeChanges;
	++contentRevision_;
	if (contentRevision_ == 0) {
		contentRevision_ = 1;
	}
}

void Engine::RenderSceneBatch::RefreshTransforms(
	ECSWorld& world, std::span<const Entity> changedEntities) {

	transformChanges_.clear();
	for (const Entity entity : changedEntities) {
		if (!world.IsAlive(entity)) {
			continue;
		}

		const auto [begin, end] =
			entityItemLookup_.equal_range(BuildEntityKey(entity));
		if (begin == end) {
			continue;
		}

		const Matrix4x4 worldMatrix =
			RenderItemExtract::GetWorldMatrix(world, entity);
		bool changed = false;
		for (auto it = begin; it != end; ++it) {
			RenderItem& item = items_[it->second];
			if (item.world != &world ||
				item.entity != entity ||
				item.worldMatrix == worldMatrix) {
				continue;
			}
			item.previousWorldMatrix = item.worldMatrix;
			item.worldMatrix = worldMatrix;
			RefreshSortPosition(item);
			item.motionFrameSerial = static_cast<uint32_t>(
				GraphicsFrameState::GetFrameSerial());
			changed = true;
		}
		if (changed) {
			transformChanges_.emplace_back(
				RenderTransformChange{
					.world = &world,
					.entity = entity,
					.worldMatrix = worldMatrix,
					.previousWorldMatrix = items_[begin->second].previousWorldMatrix,
					.motionFrameSerial = static_cast<uint32_t>(
						GraphicsFrameState::GetFrameSerial()),
				});
		}
	}
}

void Engine::RenderSceneBatch::RefreshAllTransforms() {

	transformChanges_.clear();
	for (RenderItem& item : items_) {
		if (!item.world || !item.world->IsAlive(item.entity)) {
			continue;
		}
		const Matrix4x4 worldMatrix =
			RenderItemExtract::GetWorldMatrix(*item.world, item.entity);
		if (item.worldMatrix == worldMatrix) {
			continue;
		}
		const Matrix4x4 previousWorldMatrix = item.worldMatrix;
		item.previousWorldMatrix = previousWorldMatrix;
		item.worldMatrix = worldMatrix;
		RefreshSortPosition(item);
		item.motionFrameSerial = static_cast<uint32_t>(
			GraphicsFrameState::GetFrameSerial());
		transformChanges_.emplace_back(
			RenderTransformChange{
				.world = item.world,
				.entity = item.entity,
				.worldMatrix = worldMatrix,
				.previousWorldMatrix = previousWorldMatrix,
				.motionFrameSerial = item.motionFrameSerial,
			});
	}
}

void Engine::RenderSceneBatch::RefreshSortPosition(RenderItem& item) const {

	item.sortPosition = item.worldMatrix.GetTranslationValue();
	if (item.backendID != RenderBackendID::Mesh || !item.world) {
		return;
	}
	const MeshRenderPayload* payload =
		payloadArena_.Get<MeshRenderPayload>(item.payload);
	if (!payload || payload->subMeshIndex == kAllMeshSubMeshes) {
		return;
	}
	const std::span<const SubMeshMaterial> subMeshes =
		GetMeshSubMeshes(*item.world, item.entity);
	if (subMeshes.size() <= payload->subMeshIndex) {
		return;
	}
	const SubMeshMaterial& subMesh = subMeshes[payload->subMeshIndex];
	const Matrix4x4 subMeshWorld =
		MeshSubMeshRuntime::BuildRenderLocalMatrix(subMesh) *
		item.worldMatrix;
	item.sortPosition = Vector3::Transform(
		subMesh.sourcePivot, subMeshWorld);
}

uint64_t Engine::RenderSceneBatch::BuildEntityKey(
	const Entity& entity) {

	return static_cast<uint64_t>(entity.generation) << 32 |
		static_cast<uint64_t>(entity.index);
}

void Engine::RenderSceneBatch::RebuildEntityLookup() {

	entityItemLookup_.clear();
	entityItemLookup_.reserve(items_.size());
	for (size_t index = 0; index < items_.size(); ++index) {
		entityItemLookup_.emplace(
			BuildEntityKey(items_[index].entity), index);
	}
}

//============================================================================
//	RenderQueue classMethods
//============================================================================

namespace Engine {

	bool RenderSceneBatch::MatchesStructure(const ECSWorld* world, uint64_t renderRevision) const {

		return sourceWorld_ == world &&
			sourceRenderRevision_ == renderRevision;
	}
}
