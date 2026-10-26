#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	class SplineInspectorDrawer : public SerializedComponentInspectorDrawer<SplineComponent> {
	public:

		SplineInspectorDrawer() : SerializedComponentInspectorDrawer("Spline", "Spline") {}
	private:

		SplineData data_;

		// 制御点の編集値をチャンク外から取得する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const SplineComponent& component) override;
		// 制御点と接線を編集する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
		// Undo用の保存値を組み立てる
		void SerializeDraft(ECSWorld& world, const Entity& entity, const SplineComponent& component, nlohmann::json& out) const override;
		// 制御点のプレビューを所有元へ反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const SplineComponent& component) override;
	};
}
