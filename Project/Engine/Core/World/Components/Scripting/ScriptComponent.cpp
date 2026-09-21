#include "ScriptComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	ScriptComponent classMethods
//============================================================================
void Engine::ScriptComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] ScriptComponent& component) {

	world.AddBuffer<ScriptEntry>(entity);
}

void Engine::ScriptComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasBuffer<ScriptEntry>(entity)) {
		world.RemoveBuffer<ScriptEntry>(entity);
	}
}

void Engine::ScriptComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] ScriptComponent& component) {
}

void Engine::ScriptComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] ScriptComponent& component) {
}

void Engine::ScriptComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	[[maybe_unused]] ScriptComponent& component) {

	std::vector<ScriptEntry> entries;
	if (in.is_array()) {
		entries = in.get<std::vector<ScriptEntry>>();
	}
	SetScriptEntries(world, entity, entries);
}

void Engine::ScriptComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const ScriptComponent& component,
	nlohmann::json& out) {

	SerializeScriptEntries(GetScriptEntries(world, entity), out);
}

void Engine::from_json(const nlohmann::json& in, ScriptEntry& entry) {

	// scriptTypeIDを永続主キーとして読み込む
	entry.scriptTypeID = in.value("scriptTypeId", std::string{});
	entry.lastKnownTypeName =
		in.value("lastKnownTypeName", std::string{});

	const std::string slotText =
		in.value("scriptSlotId", std::string{});
	const UUID parsedSlot = FromString16Hex(slotText);
	entry.scriptSlotID = parsedSlot ? parsedSlot : UUID::New();

	entry.scriptAsset = ParseAssetID(in, "scriptAsset");
	entry.enabled = in.value("enabled", true);
	entry.serializedFields =
		in.value("serializedFields", nlohmann::json::object());
	if (!entry.serializedFields.is_object()) {
		entry.serializedFields = nlohmann::json::object();
	}
}

void Engine::to_json(nlohmann::json& out, const ScriptEntry& entry) {

	// 型名はInspector表示とMissing Script診断に使う
	out["scriptTypeId"] = entry.scriptTypeID;
	out["scriptSlotId"] = ToString(entry.scriptSlotID);
	out["lastKnownTypeName"] = entry.lastKnownTypeName;
	out["scriptAsset"] = ToAssetReferenceJson(entry.scriptAsset);
	out["enabled"] = entry.enabled;
	out["serializedFields"] = entry.serializedFields;
}

void Engine::from_json(
	[[maybe_unused]] const nlohmann::json& in,
	[[maybe_unused]] ScriptComponent& component) {
}

void Engine::to_json(
	nlohmann::json& out,
	[[maybe_unused]] const ScriptComponent& component) {

	out = nlohmann::json::array();
}

std::span<Engine::ScriptEntry> Engine::GetScriptEntries(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<ScriptEntry>(entity).GetSpan();
}

std::span<const Engine::ScriptEntry> Engine::GetScriptEntries(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<ScriptEntry>(entity);
}

void Engine::SetScriptEntries(
	ECSWorld& world, const Entity& entity,
	std::span<const ScriptEntry> entries) {

	DynamicBuffer<ScriptEntry> buffer =
		world.TryGetBuffer<ScriptEntry>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<ScriptEntry>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(entries.size()));
	for (const ScriptEntry& entry : entries) {
		buffer.Add(entry);
	}
	world.MarkComponentModified<ScriptEntry>(entity);
}

void Engine::SerializeScriptEntries(
	std::span<const ScriptEntry> entries, nlohmann::json& out) {

	out = nlohmann::json::array();
	for (const ScriptEntry& entry : entries) {
		out.emplace_back(entry);
	}
}

//============================================================================
//	ScriptComponent classMethods
//============================================================================

namespace Engine {

	ScriptEntry MakeScriptEntry(const std::string& scriptTypeID,
		const std::string& lastKnownTypeName, AssetID scriptAsset) {

		ScriptEntry entry{};
		entry.scriptTypeID = scriptTypeID;
		entry.lastKnownTypeName = lastKnownTypeName;
		entry.scriptSlotID = UUID::New();
		entry.scriptAsset = scriptAsset;
		entry.enabled = true;
		entry.serializedFields = nlohmann::json::object();
		return entry;
	}
}
