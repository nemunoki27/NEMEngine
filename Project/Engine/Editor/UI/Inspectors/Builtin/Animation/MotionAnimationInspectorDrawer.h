#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	// 移動経路と再生条件を編集する
	class MotionAnimationInspectorDrawer : public SerializedComponentInspectorDrawer<MotionAnimationComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MotionAnimationInspectorDrawer() : SerializedComponentInspectorDrawer("MotionAnimation", "MotionAnimation") {}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		MotionAnimationSettings settings_;
		UUID targetRuntimeID_{};
		UUID splineRuntimeID_{};
		// 所有元から編集値を取り込む
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const MotionAnimationComponent& component) override;
		// 移動と回転の編集項目を描く
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
		// Undo用の編集値を書き出す
		void SerializeDraft(ECSWorld& world, const Entity& entity, const MotionAnimationComponent& component, nlohmann::json& out) const override;
		// 編集した設定を所有元へ反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const MotionAnimationComponent& component) override;
	};
}
