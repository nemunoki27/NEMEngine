#include "RenderQueue.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	RenderQueue classMethods
//============================================================================

void Engine::RenderSceneBatch::Add(RenderItem&& item) {

	items_.emplace_back(std::move(item));
}

void Engine::RenderSceneBatch::Clear() {

	items_.clear();
	payloadArena_.Clear();
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
		// マテリアル比較
		if (itemA.material.value != itemB.material.value) {
			return itemA.material.value < itemB.material.value;
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
