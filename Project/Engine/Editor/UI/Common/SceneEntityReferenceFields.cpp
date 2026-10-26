#include "SceneEntityReferenceFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

Engine::ValueEditResult Engine::SceneEntityReferenceFields::Draw(const char* label,
	ECSWorld& world, Entity owner, SceneEntityReference& reference) {

	const Entity resolved = reference.Resolve(world, owner);
	UUID selected = resolved.IsValid() ? world.GetUUID(resolved) : UUID{};
	auto result = MyGUI::EntityReferenceField(label, selected, &world);
	if (result.valueChanged) reference = SceneEntityReference::FromEntity(world, world.FindByUUID(selected));
	ImGui::PushID(label);
	if (reference.localFileID && ImGui::SmallButton("参照を解除")) {
		reference = {};
		result.valueChanged = result.editFinished = true;
	}
	ImGui::PopID();
	return result;
}
