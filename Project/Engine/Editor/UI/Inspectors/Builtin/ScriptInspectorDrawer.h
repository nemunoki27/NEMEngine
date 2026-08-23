#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
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

		ScriptInspectorDrawer() :
			SerializedComponentInspectorDrawer("", ScriptComponent::kTypeName, false) {
		}
		~ScriptInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const ScriptComponent& component) override;
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const ScriptComponent& component, nlohmann::json& out) const override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const ScriptComponent& previewComponent) override;
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

		// ScriptEntryはECS Bufferのため編集中の可変長ドラフトだけEditor側で保持する
		std::vector<ScriptEntry> draftScripts_;
	};
} // Engine
