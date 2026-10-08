#include "SceneComponentOverlayState.h"

//============================================================================
//	SceneComponentOverlayState classMethods
//============================================================================
Engine::SceneComponentOverlayState& Engine::SceneComponentOverlayState::GetInstance() {

	static SceneComponentOverlayState instance;
	return instance;
}

void Engine::SceneComponentOverlayState::SetRenderedItems(const ECSWorld* world,
	const SceneComponentOverlayItemList& items) {

	// 描画できたWorldとアイテムをそのまま次のPickerへ渡す
	world_ = world;
	renderedItems_ = items;
}

void Engine::SceneComponentOverlayState::Clear() {

	// Worldの識別値と描画済み候補を破棄
	world_ = nullptr;
	renderedItems_.clear();
}
