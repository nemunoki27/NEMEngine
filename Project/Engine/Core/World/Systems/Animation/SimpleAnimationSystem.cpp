#include "SimpleAnimationSystem.h"
#include <Engine/Core/Animation/Timeline/TimelineControl.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Properties/MaterialAnimationPropertyCatalog.h>

// c++
#include <algorithm>
#include <cmath>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

	// 評価値の型を保ったまま適用先へ渡す
	Engine::AnimationPropertyValue ToProperty(const Engine::SimpleAnimationValue& value) {

		return std::visit([](const auto& typed) -> Engine::AnimationPropertyValue {
			return typed;
		}, value);
	}

	// 適用先の現在値をアニメーションの型へ戻す
	Engine::SimpleAnimationValue FromProperty(const Engine::AnimationPropertyValue& value, [[maybe_unused]] size_t type) {
		return std::visit([](const auto& typed) -> Engine::SimpleAnimationValue { return typed; }, value);
	}

	// 他のアニメーションが同じ項目を制御しているか調べる
	bool IsControlled(Engine::ECSWorld& world, Engine::Entity entity, const Engine::AnimationPropertyBinding& binding,
		Engine::SystemContext& context) {

		if (const auto* ui = world.TryGetComponent<Engine::UISelectableComponent>(entity)) {
			const auto* runtime = world.TryGetComponent<Engine::UISelectableRuntimeComponent>(entity);
			if (runtime && runtime->initialized &&
				(ui->normal.animationEnabled || ui->selected.animationEnabled || ui->submitted.animationEnabled || ui->disabled.animationEnabled) &&
				((binding.componentName == "Transform" && binding.propertyPath.starts_with("localScale")) ||
				 ((binding.componentName == "SpriteRenderer" || binding.componentName == "TextRenderer") &&
				  binding.propertyPath == "material.color"))) return true;
		}
		if (const auto* player = world.TryGetComponent<Engine::AnimationClipPlayerComponent>(entity);
			player && player->enabled && (player->runtimePlaying || player->runtimePaused || player->runtimeApplied) && context.assetDatabase && context.animationClipManager) {
			if (const auto* clip = ResolveAnimationClipAsset(world, *context.animationClipManager, *context.assetDatabase, player->clip)) {
				for (const auto& track : clip->curveTracks) {
					if (Engine::AnimationValueOperations::SameBinding(track.binding, binding)) return true;
				}
			}
		}
		return false;
	}
}

void Engine::SimpleAnimationSystem::Restore(ECSWorld& world, BindingState& state) {

	if (world.IsAlive(state.target)) AnimationPropertySnapshot::Restore(world, state.target, state.base);
	state.base.clear();
}

void Engine::SimpleAnimationSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	RestoreEditPreviews(world);
	world.GetStorage().Get<BindingStorage>().values.clear();
	entries_.clear();
}

void Engine::SimpleAnimationSystem::RestoreEditPreviews(ECSWorld& world) {

	auto& bindings = world.GetStorage().Get<BindingStorage>().values;
	for (auto& [id, state] : bindings) {
		if (!state.edit) continue;
		Restore(world, state);
		const Entity owner = world.FindByUUID(id);
		if (world.IsAlive(owner)) if (auto* component = world.TryGetComponent<SimpleAnimationComponent>(owner)) {
			component->runtime = {};
			PublishSimpleAnimationValue(*component);
		}
	}
}

void Engine::SimpleAnimationSystem::Update(ECSWorld& world, SystemContext& context) {

	SetAnimationMaterialContext(world, context.assetDatabase, context.renderAssetLibrary);

	auto& bindings_ = world.GetStorage().Get<BindingStorage>().values;
	for (auto& [id, state] : bindings_) state.visited = false;
	entries_.clear();
	world.ForEach<SimpleAnimationComponent>([&](Entity owner, SimpleAnimationComponent& component) {
		component.runtimeApplied = false;
		const bool active = IsEntityActiveInHierarchy(world, owner) && component.enabled &&
			(context.mode == WorldMode::Play || component.playInEditMode);
		if (!active) {
			component.runtimePlaying = false;
			if (context.mode == WorldMode::Edit) component.runtime = {};
			return;
		}
		Entity target = component.runtimeTargetOverride ? component.runtimeTarget :
			(component.target.localFileID ? component.target.Resolve(world, owner) : owner);
		entries_.push_back({owner, target, &component});
	});
	std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
		return std::tie(a.target.index, a.target.generation, a.component->binding.componentName, a.component->binding.propertyPath) <
			std::tie(b.target.index, b.target.generation, b.component->binding.componentName, b.component->binding.propertyPath);
	});
	Entry* previous = nullptr;
	for (auto& entry : entries_) {
		if (!entry.component->autoApply) continue;
		if (previous && previous->target == entry.target &&
			previous->component->binding.componentName == entry.component->binding.componentName &&
			previous->component->binding.propertyPath == entry.component->binding.propertyPath) {
			previous->conflict = entry.conflict = true;
		}
		previous = &entry;
	}
	for (auto& entry : entries_) UpdateEntry(world, context, entry);
	for (auto it = bindings_.begin(); it != bindings_.end();) {
		if (!it->second.visited) {
			if (it->second.edit) Restore(world, it->second);
			it = bindings_.erase(it);
		} else ++it;
	}
}

void Engine::SimpleAnimationSystem::UpdateEntry(ECSWorld& world, SystemContext& context, Entry& entry) {

	auto& component = *entry.component;
	auto& state = world.GetStorage().Get<BindingStorage>().values[world.GetUUID(entry.owner)];
	state.visited = true;
	const auto type = component.settings.start.index();
	const auto bindingType = static_cast<AnimationValueType>(type);
	if (state.target != entry.target || state.binding.componentName != component.binding.componentName ||
		state.binding.propertyPath != component.binding.propertyPath || state.binding.valueType != bindingType) {
		if (state.edit) Restore(world, state);
		state = {};
		state.visited = true;
		state.target = entry.target;
		state.binding = component.binding;
		state.binding.valueType = bindingType;
	}
	state.edit = context.mode == WorldMode::Edit;
	const bool targetAlive = world.IsAlive(entry.target);
	if (targetAlive && !state.descriptor && component.autoApply) {
		state.descriptor = AnimationPropertyRegistry::GetInstance().ResolveProperty(world, entry.target,
			state.binding.componentName, state.binding.propertyPath, bindingType);
	}
	const bool applicable = component.autoApply && targetAlive && state.descriptor && state.descriptor->valueType == bindingType && state.descriptor->hasComponent &&
		state.descriptor->hasComponent(world, entry.target);
	component.runtimeDiagnostic.clear();
	bool conflict = entry.conflict || (applicable && IsControlled(world, entry.target, state.binding, context));
	if (component.autoApply && !applicable) component.runtimeDiagnostic = "適用先のGameObject・項目・型を確認してください";
	else if (conflict) component.runtimeDiagnostic = "他のアニメーションと適用先が競合しています";
	if ((!component.autoApply || !applicable) && state.edit && !state.base.empty()) Restore(world, state);
	if (applicable && !conflict && state.base.empty()) AnimationPropertySnapshot::Capture(world, entry.target, state.binding, state.base);
	SimpleAnimationValue initial = component.settings.start;
	if (applicable && state.descriptor->getValue) {
		AnimationPropertyValue value;
		if (state.descriptor->getValue(world, entry.target, value)) initial = FromProperty(value, type);
	}
	auto& runtime = component.runtime;
	const int32_t request = std::exchange(component.runtimeRequest, 0);
	bool started = false;
	if (!runtime.initialized) {
		StartSimpleAnimation(runtime, component.settings, initial);
		runtime.playing = component.playOnStart || request == 1;
		started = true;
	}
	if (request == 1) { StartSimpleAnimation(runtime, component.settings, initial); started = true; }
	if (request == 2) runtime.paused = true;
	if (request == 3) { runtime.paused = false; if (!runtime.sample.finished) runtime.playing = true; }
	if (request == 4) { runtime.playing = false; runtime.paused = false; }
	if (request == 5) {
		if (!conflict) {
			component.runtimeApplied = !state.base.empty();
			Restore(world, state);
		}
		if (applicable && state.descriptor->getValue) {
			AnimationPropertyValue value;
			if (state.descriptor->getValue(world, entry.target, value)) initial = FromProperty(value, type);
		}
		StartSimpleAnimation(runtime, component.settings, initial);
		runtime.playing = false;
		PublishSimpleAnimationValue(component);
		return;
	}
	if (request == 6) {
		runtime.time = (std::max)(static_cast<double>(component.runtimeSeekTime), 0.0);
		SampleSimpleAnimation(runtime, component.settings);
		started = true;
	}
	auto source = component.timeSource;
	if (component.settings.unscaledTime) source.mode = TimeSourceMode::Unscaled;
	const auto localTime = ResolveLocalTime(world, entry.owner, source, TimeChannel::SimpleAnimation,
		context.mode == WorldMode::Play ? context.deltaTime : context.unscaledDeltaTime, context.unscaledDeltaTime);
	if (!localTime.valid) component.runtimeDiagnostic = "時間源のTimeScaleが未設定です";
	const double dt = localTime.deltaTime;
	AdvanceSimpleAnimation(runtime, component.settings, started ? 0.0 : dt);
	PublishSimpleAnimationValue(component);
	if (applicable && !conflict && runtime.sample.valid && state.descriptor->setValue &&
		!IsTimelineControlled(world, entry.target, component.binding.componentName, component.binding.propertyPath) &&
		(runtime.playing || runtime.sample.finished || request == 6 || request == 1)) {
		component.runtimeApplied = state.descriptor->setValue(world, entry.target, ToProperty(runtime.sample.value));
	}
}
