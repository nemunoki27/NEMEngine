#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	class ClothInspectorDrawer : public SerializedComponentInspectorDrawer<ClothComponent> {
	public:
		ClothInspectorDrawer() : SerializedComponentInspectorDrawer("Cloth", "Cloth") {}
	private:
		ClothSettings settings_;

		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const ClothComponent&) override;
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
		void SerializeDraft(ECSWorld&, const Entity&, const ClothComponent&, nlohmann::json& out) const override;
		void ApplyPreview(ECSWorld& world, const Entity& entity, const ClothComponent&) override;
	};
}
