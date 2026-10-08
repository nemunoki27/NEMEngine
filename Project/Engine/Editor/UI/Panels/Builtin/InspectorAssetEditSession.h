#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>

namespace Engine {

	//============================================================================
	//	InspectorAssetEditSession class
	//	Asset編集の切替と終了確認を保持する
	//============================================================================
	class InspectorAssetEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InspectorAssetEditSession() = default;
		~InspectorAssetEditSession() = default;

		// 表示したDrawerの編集対象を記録する
		void TrackDrawer(IAssetInspectorDrawer* drawer, AssetID asset);
		// 未確定編集を残してPanelを閉じない
		void KeepPanelOpen(bool& open);
		// 確認を表示してPanel終了を確定する
		bool ResolvePanelClose(const EditorPanelContext& context, bool& open);
		// Hostから終了前の確認を要求する
		void RequestResolvePendingEdits();
		// Hostへ終了確認の結果を渡す
		EditorPanelCloseResult ConsumePendingEditCloseResult();

		//--------- accessor -----------------------------------------------------

		bool HasPendingEdits() const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Registry所有の編集Drawerを借用
		IAssetInspectorDrawer* activeAssetDrawer_ = nullptr;
		// 現在の編集対象Asset
		AssetID activeInspectedAsset_{};
		// 切替先Asset
		AssetID pendingAssetSelection_{};
		// 切替先の選択種別
		EditorSelectionKind pendingSelectionKind_ = EditorSelectionKind::None;
		// 切替先のEntity集合
		std::vector<Entity> pendingSelectedEntities_{};
		// 切替先のSubMesh番号
		uint32_t pendingSubMeshIndex_ = 0;
		// 切替先のSubMeshID
		UUID pendingSubMeshStableID_{};
		// 切替先のJoint所有Entity
		Entity pendingJointEntity_ = Entity::Null();
		// 切替先のJoint番号
		int32_t pendingJointIndex_ = -1;
		// Panel終了の保留
		bool pendingCloseAssetEdit_ = false;
		// 確認Popupの表示中
		bool pendingAssetEditPrompt_ = false;
		// Hostからの確認要求
		bool requestResolvePendingEdits_ = false;
		// Hostへ終了結果を返す要求
		bool pendingCloseForHost_ = false;
		// 確認済みの終了結果
		EditorPanelCloseResult pendingEditCloseResult_ = EditorPanelCloseResult::None;

		//--------- functions ----------------------------------------------------

		// 切替または終了前の編集確認を表示する
		bool ResolvePendingAssetEdit(const EditorPanelContext& context, bool closing);
		// 確認済みの編集を終了する
		void CompleteAssetEdit(EditorState& state);
		// 保留した選択へ切り替える
		void ApplyPendingSelection(EditorState& state);
	};
} // Engine
