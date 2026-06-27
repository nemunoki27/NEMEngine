#include "SceneViewInteractionPolicy.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace {

	bool TryResolveSnapGridCellSize(const Engine::EntitySnapSettings& snap, bool use2D, float& outCellSize) {

		outCellSize = use2D ? snap.translate2D.size : snap.translate3D.size;
		return outCellSize > 0.0f;
	}
}

//============================================================================
//	SceneViewInteractionPolicy methods
//============================================================================
Engine::SceneViewSnapGridDecision Engine::ResolveSceneViewSnapGridDecision(
	const EditorState& editorState, ECSWorld* world) {

	SceneViewSnapGridDecision decision{};

	// スナップグリッド表示が無効なら何も出さない
	if (!editorState.snapSettings.drawSnapGrid || !editorState.enableSnapEditEntity) {
		return decision;
	}

	const EntitySnapSettings& snap = editorState.snapSettings;

	// アセットをSceneViewへドラッグ中はドラッグ中アセットの次元で出す、選択は不要
	if (editorState.assetDragSnapGridActive) {

		decision.visible = true;
		decision.use2D = !editorState.assetDragSnapGridIs3D;
		if (!TryResolveSnapGridCellSize(snap, decision.use2D, decision.cellSize)) {
			decision.visible = false;
		}
		return decision;
	}

	// 座標移動マニピュレーター中は選択エンティティの次元で出す
	if (world &&
		editorState.sceneViewManipulatorMode == SceneViewManipulatorMode::Translate &&
		world->IsAlive(editorState.selectedEntity)) {

		decision.visible = true;
		decision.use2D = ResolveEntityDimension(*world, editorState.selectedEntity)
			.value_or(editorState.manualCameraDimension) == Dimension::Type2D;
		if (!TryResolveSnapGridCellSize(snap, decision.use2D, decision.cellSize)) {
			decision.visible = false;
		}
	}

	return decision;
}
