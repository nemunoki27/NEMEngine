#pragma once

#include "InspectorModelPreview.h"
#include "InspectorMaterialSession.h"
#include "InspectorPrefabSession.h"
#include "InspectorAssetEditSession.h"
#include "InspectorComponentSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Inspectors/Core/AssetInspectorRegistry.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>

namespace Engine {

	// front
	struct AssetMeta;

	//============================================================================
	//	InspectorPanel class
	//	インスペクターパネル
	//============================================================================
	class InspectorPanel : public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		InspectorPanel(const std::string& instanceID = "inspector.primary", bool primaryInstance = true);
		~InspectorPanel() = default;

		void Draw(const EditorPanelContext& context) override;
		void EndPreview() override;
		nlohmann::json SaveLayoutState() const override;
		void LoadLayoutState(const nlohmann::json& state) override;
		nlohmann::json MakeDuplicateState(const EditorPanelContext& context) const override;

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
		bool CanDuplicate(const EditorPanelContext& context) const override;
		bool HasPendingEdits() const override;
		void RequestResolvePendingEdits() override;
		EditorPanelCloseResult ConsumePendingEditCloseResult() override;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 表示文字サイズ
		const float fontScale_ = 0.92f;

		// 名前編集用の一時バッファ
		std::string nameEditBuffer_;
		// 名前を編集中のEntityID
		UUID editingNameEntityStableUUID_{};
		// 複製時に固定したエンティティ
		UUID lockedEntityUUID_{};

		// Materialの編集セッション
		InspectorMaterialSession materialSession_;

		// Componentの表示と編集セッション
		InspectorComponentSession componentSession_;
		// アセット種別ごとのInspector表示登録
		AssetInspectorRegistry assetInspectorRegistry_{};
		// Asset編集の確認セッション
		InspectorAssetEditSession assetEditSession_;

		// Prefab差分の編集セッション
		InspectorPrefabSession prefabSession_;

		// モデルプレビューの所有
		InspectorModelPreview modelPreview_;

		//--------- functions ----------------------------------------------------

		// エンティティのヘッダー部分を描画する
		void DrawEntityHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 選択中のアセットのインスペクターを描画する
		void DrawSelectedAssetInspector(const EditorPanelContext& context);
		// 名前の同期
		void SyncNameBufferIfNeeded(ECSWorld& world, const Entity& entity);
	};
} // Engine
