#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	// Timelineの割り当てと再生条件を編集する
	class TimelinePlayerInspectorDrawer : public SerializedComponentInspectorDrawer<TimelinePlayerComponent> {
	public:
		TimelinePlayerInspectorDrawer() : SerializedComponentInspectorDrawer("TimelinePlayer", "TimelinePlayer") {}
	private:
		TimelinePlayerSettings settings_;
		TimelineAsset asset_;
		AssetID cachedAsset_{};
		uint64_t cachedRevision_ = 0;
		std::string assetError_;
		bool assetLoaded_ = false;

		// 所有元から編集値を取得する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const TimelinePlayerComponent& component) override;
		// 保存設定をUndoへ渡す
		void SerializeDraft(ECSWorld& world, const Entity& entity, const TimelinePlayerComponent& component, nlohmann::json& out) const override;
		// 保存設定を実行状態と分けて反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const TimelinePlayerComponent& component) override;
		// 共通の再生項目と割り当てを描く
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
	};
}
