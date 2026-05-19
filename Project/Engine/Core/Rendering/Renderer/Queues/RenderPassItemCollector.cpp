#include "RenderPassItemCollector.h"

//============================================================================
//	RenderPassItemCollector classMethods
//============================================================================

const Engine::RenderPassItemList* Engine::RenderPassPhaseBuckets::Find(const std::string_view& renderPhase) const {

	auto it = phaseToItems.find(std::string(renderPhase));
	return (it == phaseToItems.end()) ? nullptr : &it->second;
}

const Engine::RenderPassItemList* Engine::RenderPassPhaseBuckets::Find(const std::string& renderPhase) const {

	auto it = phaseToItems.find(renderPhase);
	return (it == phaseToItems.end()) ? nullptr : &it->second;
}

void Engine::RenderPassItemCollector::CollectForView(const RenderSceneBatch& batch,
	const std::string& renderPhase, const ResolvedRenderView& view, RenderPassItemList& outList) {

	outList.Clear();
	for (const auto& item : batch.GetItems()) {
		// 描画フェーズとビューに対して可視なアイテムでない場合はスキップ
		if (!IsVisibleToView(item, renderPhase, view)) {
			continue;
		}
		outList.items.emplace_back(&item);
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
		// シーンインスタンスIDが一致しているアイテムのみ
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		outBuckets.phaseToItems[item.renderPhase].items.emplace_back(&item);
	}
}

bool Engine::RenderPassItemCollector::IsVisibleToView(const RenderItem& item,
	const std::string& renderPhase, const ResolvedRenderView& view) {

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
