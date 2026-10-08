#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include "Script/ScriptRuntimeValueCache.h"
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	ScriptInspectorDrawer class
	//	スクリプトコンポーネントのインスペクター描画
	//============================================================================
	class ScriptInspectorDrawer :
		public SerializedComponentInspectorDrawer<ScriptComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ScriptInspectorDrawer() : SerializedComponentInspectorDrawer("", ScriptComponent::kTypeName, false) {}
		~ScriptInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ScriptEntryはECS Bufferのため編集中の可変長ドラフトだけEditor側で保持する
		std::vector<ScriptEntry> draftScripts_;
		ScriptRuntimeValueCache runtimeValues_;

		//--------- functions ----------------------------------------------------

		// Worldから編集用Bufferを同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const ScriptComponent& component) override;
		// 編集用Bufferを含む保存データを作成する
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const ScriptComponent& component, nlohmann::json& out) const override;
		// 編集中の値をWorldへ適用する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const ScriptComponent& previewComponent) override;
		// Componentの編集項目を表示する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

	};
} // Engine
