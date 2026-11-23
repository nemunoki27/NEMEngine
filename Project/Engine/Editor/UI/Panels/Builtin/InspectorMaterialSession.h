#pragma once

//============================================================================
//	include
//============================================================================
#include "GraphMaterialAssetFields.h"
#include "InspectorMaterialPreview.h"
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine {

	struct AssetMeta;

	//============================================================================
	//	InspectorMaterialSession class
	//	Materialの編集値と保存を管理する
	//============================================================================
	class InspectorMaterialSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta);
		// 未確定のプレビューを保存済みの状態へ戻す
		void EndEdit(const EditorPanelContext& context);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		InspectorMaterialPreview preview_;
		GraphMaterialAssetFields graphFields_;
		AssetID editingMaterialAsset_{};
		MaterialAsset materialDraft_{};
		nlohmann::json savedDocument_;
		nlohmann::json savedFields_;
		std::weak_ptr<const uint8_t> databaseLifetime_;
		uint64_t contentRevision_ = 0;
		bool readOnly_ = false;
		bool pendingEdit_ = false;
		std::string diagnostic_;

		//--------- functions ----------------------------------------------------

		bool LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta);
		void SaveMaterialDraft(const EditorPanelContext& context);
	};
}
