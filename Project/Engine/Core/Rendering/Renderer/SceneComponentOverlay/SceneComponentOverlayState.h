#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayTypes.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine {

	//============================================================================
	//	SceneComponentOverlayState class
	//	EditorOverlayPassで実際に描画されたSceneView専用アイテムをPickerへ渡す
	//============================================================================
	class SceneComponentOverlayState {
	public:
		// EditorOverlayPassとEditorManager Pickerの間で共有する単一状態
		static SceneComponentOverlayState& GetInstance();

		// Rendererが実際に描画できたアイテムだけを登録する
		void SetRenderedItems(ECSWorld* world, const SceneComponentOverlayItemList& items);
		// View切替や描画失敗時に古い候補を消す
		void Clear(ECSWorld* world = nullptr);

		// Pickerが対象Worldの一致を確認するために使う
		ECSWorld* GetWorld() const { return world_; }
		// PickerがCPU判定する表示済みOverlay一覧
		const SceneComponentOverlayItemList& GetRenderedItems() const { return renderedItems_; }
	private:
		// この候補が属するWorld。違うWorldではピックしない
		ECSWorld* world_ = nullptr;
		// 画面に出たものだけを保持し、非ロード中アセットは含めない
		SceneComponentOverlayItemList renderedItems_{};
	};
}
