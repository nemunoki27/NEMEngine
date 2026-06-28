#pragma once

namespace Engine {

	// front
	struct EditorState;
	class ECSWorld;

	//============================================================================
	//	SceneViewSnapGridDecision structure
	//	SceneViewのスナップグリッド表示判定の結果
	//============================================================================
	struct SceneViewSnapGridDecision {

		// グリッドを表示するか
		bool visible = false;
		// 2Dグリッドか、falseなら3D
		bool use2D = false;
		// グリッド間隔、スナップ距離に合わせる
		float cellSize = 1.0f;
	};

	//============================================================================
	//	SceneViewInteractionPolicy
	//	SceneViewのスナップグリッド表示条件を判定する
	//============================================================================

	// 現在の操作状態からスナップグリッドの表示判定を返す
	SceneViewSnapGridDecision ResolveSceneViewSnapGridDecision(const EditorState& editorState, ECSWorld* world);

} // Engine
