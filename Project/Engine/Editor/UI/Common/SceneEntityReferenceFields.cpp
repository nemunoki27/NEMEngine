#include "SceneEntityReferenceFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

namespace Engine::SceneEntityReferenceFields {

	template<class DrawField>
	static ValueEditResult DrawReference(ECSWorld& world, Entity owner, SceneEntityReference& reference,
		EntityEditSetting options, UUID* runtimeReference, DrawField&& drawField) {

		const bool hasRuntime = runtimeReference && *runtimeReference;
		const Entity resolved = hasRuntime ? world.FindByUUID(*runtimeReference) : reference.Resolve(world, owner);
		UUID selected = world.IsAlive(resolved) ? world.GetUUID(resolved) : UUID{};
		const std::string missing = ToString(hasRuntime ? *runtimeReference : reference.localFileID);
		options.hasStoredReference = hasRuntime || static_cast<bool>(reference.localFileID);
		options.missingReferenceID = missing;
		auto result = drawField(selected, options);
		if (result.valueChanged) {
			reference = selected ? SceneEntityReference::FromEntity(world, world.FindByUUID(selected)) : SceneEntityReference{};
			if (runtimeReference) *runtimeReference = reference.localFileID ? UUID{} : selected;
		}
		return result;
	}

	ValueEditResult Draw(const char* label, ECSWorld& world, Entity owner, SceneEntityReference& reference,
		const EntityEditSetting& setting, UUID* runtimeReference) {

		return DrawReference(world, owner, reference, setting, runtimeReference, [&](UUID& selected, const auto& options) {
			return MyGUI::EntityReferenceField(label, selected, &world, options);
		});
	}

	ValueEditResult DrawObjectOrAsset(const char* label, ECSWorld& world, Entity owner, SceneEntityReference& reference,
		UUID& runtimeReference, AssetID& asset, const AssetDatabase* database, const std::initializer_list<AssetType>& types,
		const EntityEditSetting& setting, const std::function<const char*(AssetID)>& validateAsset) {

		return DrawReference(world, owner, reference, setting, &runtimeReference, [&](UUID& selected, const auto& options) {
			return MyGUI::ObjectOrAssetReferenceField(label, selected, asset, &world, database, types, options, validateAsset);
		});
	}
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
