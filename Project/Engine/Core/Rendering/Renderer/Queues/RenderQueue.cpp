#include "RenderQueue.h"

//============================================================================
//	RenderQueue classMethods
//============================================================================
void Engine::RenderSceneBatch::Add(RenderItem&& item) {

	items_.emplace_back(std::move(item));
}

void Engine::RenderSceneBatch::Clear() {

	items_.clear();
	payloadArena_.Clear();
	sourceWorld_ = nullptr;
	sourceRenderRevision_ = 0;
	sourceTransformRevision_ = 0;
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
}

void Engine::RenderSceneBatch::SetSource(const ECSWorld* world,
	uint64_t renderRevision, uint64_t transformRevision) {

	sourceWorld_ = world;
	sourceRenderRevision_ = renderRevision;
	sourceTransformRevision_ = transformRevision;
	++contentRevision_;
	if (contentRevision_ == 0) {
		contentRevision_ = 1;
	}
}

void Engine::RenderSceneBatch::SetTransformSource(uint64_t transformRevision) {

	sourceTransformRevision_ = transformRevision;
	++contentRevision_;
	if (contentRevision_ == 0) {
		contentRevision_ = 1;
	}
}
