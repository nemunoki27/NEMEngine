#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>

namespace Engine {

	//============================================================================
	//	CanvasInspectorDrawer class
	//	Canvasの入力と遷移表を編集する
	//============================================================================
	class CanvasInspectorDrawer : public SerializedComponentInspectorDrawer<CanvasComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CanvasInspectorDrawer();
		~CanvasInspectorDrawer() = default;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 上移動のキー
		std::vector<KeyDIKCode> navigationUpKeys_{};
		// 下移動のキー
		std::vector<KeyDIKCode> navigationDownKeys_{};
		// 左移動のキー
		std::vector<KeyDIKCode> navigationLeftKeys_{};
		// 右移動のキー
		std::vector<KeyDIKCode> navigationRightKeys_{};
		// 上移動のゲームパッドボタン
		std::vector<GamePadButtons> navigationUpGamepadButtons_{};
		// 下移動のゲームパッドボタン
		std::vector<GamePadButtons> navigationDownGamepadButtons_{};
		// 左移動のゲームパッドボタン
		std::vector<GamePadButtons> navigationLeftGamepadButtons_{};
		// 右移動のゲームパッドボタン
		std::vector<GamePadButtons> navigationRightGamepadButtons_{};
		// 決定のキー
		std::vector<KeyDIKCode> submitKeys_{};
		// 決定のゲームパッドボタン
		std::vector<GamePadButtons> submitGamepadButtons_{};
		// 編集用の遷移表
		CanvasNavigationTable navigationTable_{};

		//--------- functions ----------------------------------------------------

		// 設定項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		// Bufferを編集用データへ読み込む
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const CanvasComponent& component) override;
		// 編集値を保存用データへまとめる
		void SerializeDraft(
			ECSWorld& world, const Entity& entity, const CanvasComponent& component, nlohmann::json& out) const override;
		// 実行値へプレビューを反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const CanvasComponent& previewComponent) override;
	};

} // Engine
