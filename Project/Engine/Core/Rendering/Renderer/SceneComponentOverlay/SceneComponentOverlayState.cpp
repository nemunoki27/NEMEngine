#include "SceneComponentOverlayState.h"

//============================================================================
//	SceneComponentOverlayState classMethods
//============================================================================
Engine::SceneComponentOverlayState& Engine::SceneComponentOverlayState::GetInstance() {

	static SceneComponentOverlayState instance;
	return instance;
}

void Engine::SceneComponentOverlayState::SetRenderedItems(ECSWorld* world,
	const SceneComponentOverlayItemList& items) {

	// 描画できたWorldとアイテムをそのまま次のPickerへ渡す
	world_ = world;
	renderedItems_ = items;
}

void Engine::SceneComponentOverlayState::Clear(ECSWorld* world) {

	(void)world;
	// World指定は将来拡張用
	world_ = nullptr;
	renderedItems_.clear();
}
