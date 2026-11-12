#include "DayNightCycleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DayNightCycleComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {

	struct LightOverride {
		Engine::Entity owner;
		Engine::Entity target;
		Engine::Vector3 direction;
	};
	struct AtmosphereOverride {
		Engine::Entity owner;
		Engine::Entity target;
		Engine::Entity sun;
		Engine::Entity moon;
	};
	struct Entry {
		Engine::Entity owner;
		Engine::Entity sun;
		Engine::Entity moon;
		Engine::Entity atmosphere;
		Engine::DayNightCycleComponent* component;
		bool conflict = false;
	};
	struct CycleStorage {
		std::unordered_map<Engine::UUID, LightOverride> lights;
		std::unordered_map<Engine::UUID, LightOverride> previous;
		std::unordered_map<Engine::UUID, AtmosphereOverride> atmospheres;
		std::vector<Entry> entries;
		std::unordered_map<Engine::UUID, size_t> owners;
	};

	// 削除直後の描画でも古い制御結果を使用しない
	bool IsControllerActive(Engine::ECSWorld& world, Engine::Entity owner) {

		const auto* cycle = world.TryGetComponent<Engine::DayNightCycleComponent>(owner);
		return cycle && !world.IsPendingDestroy(owner) && cycle->enabled && Engine::IsEntityActiveInHierarchy(world, owner);
	}

	// 結果が変化したライトだけ抽出キャッシュを更新する
	void NotifyChanges(Engine::ECSWorld& world, CycleStorage& storage) {

		for (const auto& [id, light] : storage.previous) {
			const auto next = storage.lights.find(id);
			if (world.IsAlive(light.target) && (next == storage.lights.end() || next->second.direction != light.direction))
				world.MarkComponentModified<Engine::DirectionalLightComponent>(light.target);
		}
		for (const auto& [id, light] : storage.lights) {
			if (!storage.previous.contains(id)) world.MarkComponentModified<Engine::DirectionalLightComponent>(light.target);
		}
		storage.previous.clear();
	}
}

bool Engine::ResolveDayNightLightDirection(ECSWorld& world, Entity light, Vector3& direction) {

	const auto* storage = world.GetStorage().TryGet<CycleStorage>();
	if (!storage || !world.IsAlive(light)) return false;
	const auto& values = storage->lights;
	const auto found = values.find(world.GetUUID(light));
	if (found == values.end() || !IsControllerActive(world, found->second.owner)) return false;
	direction = found->second.direction;
	return true;
}

bool Engine::ResolveDayNightAtmosphereLights(ECSWorld& world, Entity atmosphere, Entity& sun, Entity& moon) {

	const auto* storage = world.GetStorage().TryGet<CycleStorage>();
	if (!storage || !world.IsAlive(atmosphere)) return false;
	const auto& values = storage->atmospheres;
	const auto found = values.find(world.GetUUID(atmosphere));
	if (found == values.end() || !IsControllerActive(world, found->second.owner)) return false;
	sun = found->second.sun;
	moon = found->second.moon;
	return true;
}

void Engine::DayNightCycleSystem::RestoreEditPreviews(ECSWorld& world) {

	auto& storage = world.GetStorage().Get<CycleStorage>();
	storage.previous.swap(storage.lights);
	storage.lights.clear();
	storage.atmospheres.clear();
	NotifyChanges(world, storage);
	world.ForEach<DayNightCycleComponent>([](Entity, DayNightCycleComponent& component) {
		component.runtimeInitialized = false;
		component.runtimePlaying = false;
		component.runtimePaused = false;
	});
}

void Engine::DayNightCycleSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	RestoreEditPreviews(world);
}

void Engine::DayNightCycleSystem::Update(ECSWorld& world, SystemContext& context) {

	auto& storage = world.GetStorage().Get<CycleStorage>();
	storage.previous.swap(storage.lights);
	storage.lights.clear();
	storage.atmospheres.clear();
	auto& entries = storage.entries;
	entries.clear();
	world.ForEach<DayNightCycleComponent>([&](Entity owner, DayNightCycleComponent& component) {
		component.runtimeDiagnostic.clear();
		if (!component.enabled || !IsEntityActiveInHierarchy(world, owner)) return;
		const bool advance = context.mode == WorldMode::Play || component.playInEditMode;
		const auto time = ResolveLocalTime(world, owner, component.timeSource, TimeChannel::DayNightCycle,
			context.mode == WorldMode::Play ? context.deltaTime : context.unscaledDeltaTime, context.unscaledDeltaTime);
		UpdateDayNightClock(component, time.valid ? time.deltaTime : 0.0, advance);
		if (!time.valid) component.runtimeDiagnostic = "時間源のオブジェクトまたはTimeScaleが見つかりません";
		if (!component.runtimeDiagnostic.empty()) return;
		const Entity sun = component.sunEntity.localFileID ? component.sunEntity.Resolve(world, owner) : owner;
		const Entity moon = component.moonEntity.Resolve(world, owner);
		const Entity atmosphere = component.atmosphereEntity.Resolve(world, owner);
		if (!world.TryGetComponent<DirectionalLightComponent>(sun)) {
			component.runtimeDiagnostic = "太陽にDirectionalLightを設定してください";
			return;
		}
		if (component.moonEntity.localFileID && !world.TryGetComponent<DirectionalLightComponent>(moon)) {
			component.runtimeDiagnostic = "月にDirectionalLightを設定してください";
			return;
		}
		if (component.atmosphereEntity.localFileID && !world.TryGetComponent<SkyAtmosphereComponent>(atmosphere)) {
			component.runtimeDiagnostic = "空にSkyAtmosphereを設定してください";
			return;
		}
		entries.push_back({owner, sun, moon, atmosphere, &component, sun == moon});
	});
	auto& owners = storage.owners;
	owners.clear();
	for (size_t index = 0; index < entries.size(); ++index) {
		auto& entry = entries[index];
		for (Entity target : {entry.sun, entry.moon, entry.atmosphere}) {
			if (!world.IsAlive(target)) continue;
			const auto [found, inserted] = owners.emplace(world.GetUUID(target), index);
			if (!inserted && found->second != index) entry.conflict = entries[found->second].conflict = true;
		}
	}
	for (auto& entry : entries) {
		if (entry.conflict) {
			entry.component->runtimeDiagnostic = "太陽・月・空の適用先が他の昼夜制御と競合しています";
			continue;
		}
		storage.lights.emplace(world.GetUUID(entry.sun), LightOverride{entry.owner, entry.sun, entry.component->runtimeSunDirection});
		if (world.IsAlive(entry.moon)) storage.lights.emplace(world.GetUUID(entry.moon),
			LightOverride{entry.owner, entry.moon, entry.component->runtimeMoonDirection});
		if (world.IsAlive(entry.atmosphere)) storage.atmospheres.emplace(world.GetUUID(entry.atmosphere),
			AtmosphereOverride{entry.owner, entry.atmosphere, entry.sun, entry.moon});
	}
	NotifyChanges(world, storage);
}
