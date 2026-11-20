#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>

namespace Engine {

	class TrailRendererInspectorDrawer : public SerializedComponentInspectorDrawer<TrailRendererComponent> {
	public:
		TrailRendererInspectorDrawer() : SerializedComponentInspectorDrawer("TrailRenderer", "TrailRenderer") {}
	private:
		TrailRendererSettings settings_;
		CurveEditorState widthState_, colorState_;
		bool invalidMaterial_ = false;
		UUID inspected_{};
		UUID rootID_{}, tipID_{};

		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const TrailRendererComponent&) override;
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
		void SerializeDraft(ECSWorld&, const Entity&, const TrailRendererComponent&, nlohmann::json& out) const override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const TrailRendererComponent&) override;
	};
}
