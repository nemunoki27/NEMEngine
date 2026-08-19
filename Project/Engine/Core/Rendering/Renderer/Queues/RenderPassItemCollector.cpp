#include "RenderPassItemCollector.h"

// c++
#include <algorithm>

namespace {

	// 半透明を描画優先度の後にカメラから遠い順で並べる
	void SortTransparentItems(
		std::vector<const Engine::RenderItem*>& items,
		const Engine::ResolvedRenderView& view) {

		std::stable_sort(items.begin(), items.end(),
			[&](const Engine::RenderItem* a,
				const Engine::RenderItem* b) {

				if (!a || !b) {
					return a != nullptr;
				}
				if (a->sortingLayer != b->sortingLayer) {
					return a->sortingLayer < b->sortingLayer;
				}
				if (a->sortingOrder != b->sortingOrder) {
					return a->sortingOrder < b->sortingOrder;
				}
				if (a->orderedUI || b->orderedUI) {
					if (a->orderedUI != b->orderedUI) {
						return !a->orderedUI;
					}
					return a->hierarchyOrder < b->hierarchyOrder;
				}

				const Engine::ResolvedCameraView* cameraA =
					view.FindCamera(a->cameraDomain);
				const Engine::ResolvedCameraView* cameraB =
					view.FindCamera(b->cameraDomain);
				if (!cameraA || !cameraB) {
					return false;
				}
				const Engine::Vector3 deltaA =
					a->sortPosition - cameraA->cameraPos;
				const Engine::Vector3 deltaB =
					b->sortPosition - cameraB->cameraPos;
				const float distanceA = Engine::Vector3::Dot(deltaA, deltaA);
				const float distanceB = Engine::Vector3::Dot(deltaB, deltaB);
				return distanceA > distanceB;
			});
	}
}

//============================================================================
//	RenderPassItemCollector classMethods
//============================================================================
void Engine::RenderPassPhaseBuckets::Clear() {

	for (RenderPassItemList& bucket : buckets) {
		bucket.Clear();
	}
}

Engine::RenderPassItemList& Engine::RenderPassPhaseBuckets::Get(RenderPhase phase) {

	return buckets[Engine::EnumAdapter<Engine::RenderPhase>::GetIndex(phase)];
}

const Engine::RenderPassItemList& Engine::RenderPassPhaseBuckets::Get(RenderPhase phase) const {

	return buckets[Engine::EnumAdapter<Engine::RenderPhase>::GetIndex(phase)];
}

void Engine::RenderPassItemCollector::CollectForView(const RenderSceneBatch& batch,
	RenderPhase renderPhase, const ResolvedRenderView& view, RenderPassItemList& outList) {

	outList.Clear();
	for (const auto& item : batch.GetItems()) {
		// 描画フェーズとビューに対して可視なアイテムでない場合はスキップ
		if (!IsVisibleToView(item, renderPhase, view)) {
			continue;
		}
		outList.items.emplace_back(&item);
	}
	if (renderPhase == RenderPhase::Transparent) {
		SortTransparentItems(outList.items, view);
	}
}

void Engine::RenderPassItemCollector::BuildBucketsForViewAndScene(const RenderSceneBatch& batch,
	const ResolvedRenderView& view, UUID sceneInstanceID, RenderPassPhaseBuckets& outBuckets) {

	outBuckets.Clear();
	if (!view.valid) {
		return;
	}
	for (const auto& item : batch.GetItems()) {

		// ビュー可視でなければバケットに入れない
		if (!IsVisibleToView(item, view)) {
			continue;
		}
		// アクティブscene由来のitemのみ描く、subsceneは別RTへ描いて参照する設計のためここでは混ぜない
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		outBuckets.Get(item.renderPhase).items.emplace_back(&item);
	}
	SortTransparentItems(
		outBuckets.Get(RenderPhase::Transparent).items, view);
}

bool Engine::RenderPassItemCollector::IsVisibleToView(const RenderItem& item,
	RenderPhase renderPhase, const ResolvedRenderView& view) {

	// 描画フェーズが一致しない、無効な場合は非表示
	if (!view.valid || item.renderPhase != renderPhase) {
		return false;
	}
	// カリングマスクに一致しない場合は非表示
	const ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
	if (!camera) {
		return false;
	}
	if ((item.visibilityLayerMask & camera->cullingMask) == 0) {
		return false;
	}
	return true;
}

bool Engine::RenderPassItemCollector::IsVisibleToView(const RenderItem& item, const ResolvedRenderView& view) {

	// カリングマスクに一致しない場合は非表示
	const ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
	if (!camera) {
		return false;
	}
	if ((item.visibilityLayerMask & camera->cullingMask) == 0) {
		return false;
	}
	return true;
}
