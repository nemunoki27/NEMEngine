#include "SceneEntityReferenceFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

Engine::ValueEditResult Engine::SceneEntityReferenceFields::Draw(const char* label,
	ECSWorld& world, Entity owner, SceneEntityReference& reference, const EntityEditSetting& setting) {

	const Entity resolved = reference.Resolve(world, owner);
	UUID selected = world.IsAlive(resolved) ? world.GetUUID(resolved) : UUID{};
	auto options = setting;
	const std::string missing = ToString(reference.localFileID);
	options.hasStoredReference = static_cast<bool>(reference.localFileID);
	options.missingReferenceID = missing;
	auto result = MyGUI::EntityReferenceField(label, selected, &world, options);
	if (result.valueChanged) reference = selected ? SceneEntityReference::FromEntity(world, world.FindByUUID(selected)) : SceneEntityReference{};
	return result;
}

Engine::ValueEditResult Engine::SceneEntityReferenceFields::DrawLocal(const char* label,
	ECSWorld& world, UUID& localFileID) {

	const Entity target = SceneObjectUtility::FindByLocalFileID(world, localFileID);
	UUID selected = world.IsAlive(target) ? world.GetUUID(target) : UUID{};
	const std::string missing = ToString(localFileID);
	EntityEditSetting setting;
	setting.hasStoredReference = static_cast<bool>(localFileID);
	setting.missingReferenceID = missing;
	setting.acceptEntity = [](ECSWorld& source, Entity entity) { return source.HasComponent<SceneObjectComponent>(entity); };
	auto result = MyGUI::EntityReferenceField(label, selected, &world, setting);
	if (result.valueChanged) {
		const auto* scene = world.TryGetComponent<SceneObjectComponent>(world.FindByUUID(selected));
		localFileID = scene ? scene->localFileID : UUID{};
	}
	return result;
}
