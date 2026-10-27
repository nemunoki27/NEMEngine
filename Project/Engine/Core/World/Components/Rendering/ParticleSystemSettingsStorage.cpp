#include "ParticleSystemComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingComponent.h>

// c++
#include <stdexcept>

using namespace Engine;

namespace {

	ParticleSystemSettingsRecord& EnsureSettings(ECSWorld& world, const Entity& entity, ParticleSystemComponent& component) {

		auto& storage = world.GetStorage().Get<ParticleSystemSettingsStorage>();
		auto* current = storage.TryGet(component.settings);
		const Engine::UUID owner = world.GetUUID(entity);
		if (current && current->owner == owner) return *current;
		// 値コピーで追加された場合も、設定の変更先を分ける
		auto value = current ? current->value : std::make_shared<const ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
		component.settings = storage.Emplace(ParticleSystemSettingsRecord{std::move(value), owner});
		return *storage.TryGet(component.settings);
	}
}

void ParticleSystemComponent::InitializeStorage(ECSWorld& world, const Entity& entity, ParticleSystemComponent& component) {

	EnsureSettings(world, entity, component);
}

void ParticleSystemComponent::ReleaseStorage(ECSWorld& world, const Entity& entity, ParticleSystemComponent& component) {

	auto& storage = world.GetStorage().Get<ParticleSystemSettingsStorage>();
	const auto* record = storage.TryGet(component.settings);
	// 所有先と異なる一時コピーから解放しない
	if (record && record->owner == world.GetUUID(entity)) storage.Release(component.settings);
	component.settings = ParticleSystemSettingsHandle::Null();
}

void ParticleSystemComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld,
	const Entity& entity, const ParticleSystemComponent& source, ParticleSystemComponent& destination) {

	destination.settings = ParticleSystemSettingsHandle::Null();
	const auto* storage = sourceWorld.GetStorage().TryGet<ParticleSystemSettingsStorage>();
	const auto* record = storage ? storage->TryGet(source.settings) : nullptr;
	auto value = record ? record->value : std::make_shared<const ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
	destination.settings = destinationWorld.GetStorage().Get<ParticleSystemSettingsStorage>().Emplace(
		ParticleSystemSettingsRecord{std::move(value), destinationWorld.GetUUID(entity), record ? record->revision : 1});
}

void ParticleSystemComponent::DeserializeECS(ECSWorld& world, const Entity& entity,
	const nlohmann::json& in, ParticleSystemComponent& component) {

	ParticleSystemSettings candidate = MakeDefaultParticleSystemSettings();
	if (in.contains("settings") && !FromJson(in.at("settings"), candidate)) {
		throw std::invalid_argument("ParticleSystemの設定が不正です");
	}
	// 設定の読込に成功してからHot値と所有を更新する
	auto replacement = component;
	from_json(in, replacement);
	auto settings = std::make_shared<const ParticleSystemSettings>(std::move(candidate));
	auto& record = EnsureSettings(world, entity, component);
	replacement.settings = component.settings;
	component = replacement;
	record.value = std::move(settings);
	++record.revision;
}

void ParticleSystemComponent::SerializeECS(const ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const ParticleSystemComponent& component, nlohmann::json& out) {

	to_json(out, component);
	const auto* storage = world.GetStorage().TryGet<ParticleSystemSettingsStorage>();
	const auto* record = storage ? storage->TryGet(component.settings) : nullptr;
	if (!record) throw std::logic_error("ParticleSystemの保存設定が見つかりません");
	out["settings"] = ToJson(*record->value);
}

std::shared_ptr<const ParticleSystemSettings> Engine::GetParticleSystemSettings(const ECSWorld& world, const Entity& entity) {

	const auto* component = world.TryGetComponentForBinding<ParticleSystemComponent>(entity);
	const auto* storage = world.GetStorage().TryGet<ParticleSystemSettingsStorage>();
	const auto* record = component && storage ? storage->TryGet(component->settings) : nullptr;
	return record ? record->value : component ? std::make_shared<const ParticleSystemSettings>(MakeDefaultParticleSystemSettings()) : nullptr;
}

void Engine::SetParticleSystemSettings(ECSWorld& world, const Entity& entity, const ParticleSystemSettings& settings) {

	ParticleSystemSettings checked;
	if (!FromJson(ToJson(settings), checked)) throw std::invalid_argument("ParticleSystemの設定が不正です");
	auto* component = world.TryGetComponentForBinding<ParticleSystemComponent>(entity);
	if (!component) throw std::invalid_argument("ParticleSystemがありません");
	auto& record = EnsureSettings(world, entity, *component);
	if (!world.HasComponent<ParticleSystemComponent>(entity)) {
		const auto type = ComponentTypeRegistry::GetInstance().GetID<ParticleSystemComponent>();
		auto* pending = world.GetCommandBuffer().FindPendingComponent(entity, type);
		const auto lifetime = world.GetLifetime();
		// 追加取消時は設定を解放し、追加済みならComponentへ所有を残す
		pending->SetExternalCleanup([&world, entity, lifetime](void* data) {
			if (!lifetime->IsAlive()) return;
			auto& value = *static_cast<ParticleSystemComponent*>(data);
			const auto* published = world.TryGetComponent<ParticleSystemComponent>(entity);
			if (!published || published->settings != value.settings) {
				world.GetStorage().Get<ParticleSystemSettingsStorage>().Release(value.settings);
			}
		});
	}
	if (ToJson(*record.value) == ToJson(checked)) return;
	record.value = std::make_shared<const ParticleSystemSettings>(std::move(checked));
	++record.revision;
	world.MarkComponentModified<ParticleSystemComponent>(entity);
}
