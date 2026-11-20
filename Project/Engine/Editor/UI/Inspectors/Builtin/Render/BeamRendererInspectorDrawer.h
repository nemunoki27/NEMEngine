#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Common/ReflectedMaterialParameterDrawer.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>

namespace Engine {

	class BeamRendererInspectorDrawer : public SerializedComponentInspectorDrawer<BeamRendererComponent> {
	public:
		BeamRendererInspectorDrawer() : SerializedComponentInspectorDrawer("BeamRenderer", "BeamRenderer") {}
	private:
		BeamRendererSettings settings_;
		ReflectedMaterialParameterDrawer materialParameters_;
		CurveEditorState widthState_;
		bool invalidMaterial_ = false;
		UUID inspected_{};

		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const BeamRendererComponent&) override;
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
		void SerializeDraft(ECSWorld&, const Entity&, const BeamRendererComponent&, nlohmann::json& out) const override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const BeamRendererComponent&) override;
	};
}
