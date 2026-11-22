#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include "ManagedCameraSettings.h"
#include "ManagedMotionAnimationSettings.h"
#include "ManagedTimelineSettings.h"
#include "ManagedRendererSettings.h"
#include "ManagedLightSettings.h"
#include <Engine/Core/Animation/Text/TextCharAnimationSampler.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/Systems/Animation/TextCharAnimationSystem.h>
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/HexTilingComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/FlipbookAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>
#include <Engine/Core/World/Components/Time/TimerComponent.h>
#include <Engine/Core/World/Components/Lighting/DayNightCycleComponent.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Animation/TextCharAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>

using namespace Engine;
using namespace Engine::ManagedCameraSettings;

namespace {

	nlohmann::json WriteNativeReference(const ManagedNativeEntity& reference) {

		return {{"world", {{"index", reference.world.index}, {"generation", reference.world.generation}}},
			{"index", reference.index}, {"generation", reference.generation}};
	}

	ManagedNativeEntity ReadNativeReference(const nlohmann::json& value) {

		ManagedNativeEntity reference{};
		reference.world.index = value.at("world").at("index").get<uint32_t>();
		reference.world.generation = value.at("world").at("generation").get<uint32_t>();
		reference.index = value.at("index").get<uint32_t>();
		reference.generation = value.at("generation").get<uint32_t>();
		return reference;
	}

	bool ResolveTextSources(ECSWorld& world, const SystemContext* context, TextCharAnimationComponent& component) {

		return !component.legacyClip || (context && context->assetDatabase && context->animationClipManager &&
			ResolveLegacyTextCharClip(world, *context->assetDatabase, *context->animationClipManager, component));
	}

	nlohmann::json ReadTextSources(ECSWorld& world, Entity owner, const SystemContext* context,
		TextCharAnimationComponent& component) {

		ResolveTextSources(world, context, component);
		auto sources = nlohmann::json::array();
		for (size_t slot = 0; slot < component.sourceAssets.size(); ++slot) {
			nlohmann::json source;
			if (const auto asset = component.sourceAssets[slot]) {
				const auto* meta = context && context->assetDatabase ? context->assetDatabase->Find(asset) : nullptr;
				if (meta) source = {{"asset", ToString(asset)}, {"type", EnumAdapter<AssetType>::ToString(meta->type)}};
			} else {
				const auto id = component.RuntimeSource(slot);
				const auto target = id ? world.FindByUUID(id) : component.SceneSource(slot).Resolve(world, owner);
				if (world.IsAlive(target)) source = {{"entity", WriteNativeReference(MakeNativeEntity(world, target))}};
			}
			sources.push_back(std::move(source));
		}
		return sources;
	}

	bool SetTextSource(ECSWorld& world, Entity owner, const SystemContext* context, const nlohmann::json& patch) {

		auto* component = world.TryGetComponentForBinding<TextCharAnimationComponent>(owner);
		const auto slot = patch.at("sourceSlot").get<size_t>();
		if (!component || slot >= component->sourceAssets.size()) return false;
		AssetID asset{};
		Entity target = Entity::Null();
		if (patch.contains("entity")) {
			const auto reference = ReadNativeReference(patch.at("entity"));
			target = ResolveEntity(reference);
			if (ResolveWorld(reference) != &world || !world.IsAlive(target)) return false;
			const auto* animation = world.TryGetComponentForBinding<SimpleAnimationComponent>(target);
			if (!animation || TextCharAnimationSampler::ValidateSimple(slot, animation->settings)) return false;
		} else if (patch.contains("asset")) {
			asset = ParseAssetID(patch, "asset");
			if (!asset || !context || !context->assetDatabase || !context->animationClipManager ||
				TextCharAnimationSampler::ValidateAsset(slot, world, *context->assetDatabase, *context->animationClipManager, asset)) return false;
		}
		if (!ResolveTextSources(world, context, *component)) return false;
		component->sourceAssets[slot] = asset;
		component->SceneSource(slot) = world.IsAlive(target) ? SceneEntityReference::FromEntity(world, target) : SceneEntityReference{};
		component->RuntimeSource(slot) = world.IsAlive(target) ? world.GetUUID(target) : Engine::UUID{};
		return true;
	}

	TimeSourceSettings* ResolveComponentTimeSource(ECSWorld& world, Entity entity, int32_t type) {
		switch (type) {
		case 0: if (auto* component = world.TryGetComponentForBinding<AnimationClipPlayerComponent>(entity)) return &component->timeSource; break;
		case 8: return TryGetParticleSystemTimeSource(world, entity);
		case 9: if (auto* component = world.TryGetComponentForBinding<FlipbookAnimationComponent>(entity)) return &component->timeSource; break;
		case 26: if (auto* component = world.TryGetComponentForBinding<SkinnedAnimationComponent>(entity)) return &component->timeSource; break;
		case 81: if (auto* component = world.TryGetComponentForBinding<TextCharAnimationComponent>(entity)) return &component->timeSource; break;
		case 70: if (auto* component = world.TryGetComponentForBinding<SimpleAnimationComponent>(entity)) return &component->timeSource; break;
		case 71: if (auto* component = world.TryGetComponentForBinding<TimerComponent>(entity)) return &component->timeSource; break;
		case 75: if (auto* component = world.TryGetComponentForBinding<DayNightCycleComponent>(entity)) return &component->timeSource; break;
		case 72: if (auto* record = TryGetMotionAnimation(world, entity)) return &record->settings.timeSource; break;
		case 77: if (auto* record = TryGetTrailRenderer(world, entity)) return &record->settings.timeSource; break;
		case 78: if (auto* record = TryGetBeamRenderer(world, entity)) return &record->settings.timeSource; break;
		case 76: if (auto* record = TryGetTimelinePlayer(world, entity)) return &record->settings.timeSource; break;
		}
		return nullptr;
	}

	void ClearLegacyUnscaledTime(ECSWorld& world, Entity entity, int32_t type) {
		if (type == 0) {
			if (auto* component = world.TryGetComponentForBinding<AnimationClipPlayerComponent>(entity)) component->unscaledTime = false;
		}
		if (type == 8) {
			if (auto* component = world.TryGetComponentForBinding<ParticleSystemComponent>(entity)) component->useUnscaledTime = false;
		}
		if (type == 70) {
			if (auto* component = world.TryGetComponentForBinding<SimpleAnimationComponent>(entity)) component->settings.unscaledTime = false;
		}
	}
}


//============================================================================
//	ManagedScriptRuntime componentSettingsMethods
//============================================================================
int32_t ManagedScriptRuntime::CopyComponentSettingsCallback(ManagedNativeEntity entity, int32_t type,
	char* buffer, int32_t capacity) {

	try {
		auto* world = ResolveWorld(entity);
		const auto target = ResolveEntity(entity);
		if (!world || !world->IsAlive(target)) return -1;
		nlohmann::json data;
		if (const auto* lightSettings = ResolveLightInfluenceSettings(*world, target, type)) {
			data = *lightSettings;
			data["shadowExclusionTargets"] = nlohmann::json::array();
			for (const auto& exclusion : lightSettings->shadowExclusions) {
				const auto reference = MakeNativeEntity(*world, ResolveLightShadowExclusion(*world, target, exclusion));
				data["shadowExclusionTargets"].push_back(WriteNativeReference(reference));
			}
		} else if (type == 0) {
			const auto* component = world->TryGetComponentForBinding<AnimationClipPlayerComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 14) {
			const auto* component = world->TryGetComponentForBinding<MeshRendererComponent>(target);
			if (!component) return -1;
			SerializeMeshRenderer(*component, GetMeshSubMeshes(*world, target), data);
		} else if (type == 79) {
			const auto* component = world->TryGetComponentForBinding<HexTilingComponent>(target);
			if (!component) return -1;
			auto& enabled = data["subMeshEnabled"] = nlohmann::json::array();
			const auto subMeshes = GetMeshSubMeshes(*world, target);
			for (uint32_t index = 0; index < subMeshes.size(); ++index) {
				enabled.push_back(IsHexTilingSubMeshEnabled(*component, subMeshes[index], index));
			}
		} else if (type == 77) {
			const auto* record = TryGetTrailRenderer(*world, target);
			if (!record) return -1;
			data = record->settings;
		} else if (type == 78) {
			const auto* record = TryGetBeamRenderer(*world, target);
			if (!record) return -1;
			data = record->settings;
		} else if (const auto* settings = ResolveRendererSettings(*world, target, type)) {
			data = {{"renderSettings", *settings}};
		} else if (type == 16 || type == 17) {
			const auto* camera = ResolveCamera(*world, target, type);
			const auto* context = GetCurrentContext();
			if (!camera || !context || !context->renderAssetLibrary || !context->assetDatabase) return -1;
			auto& library = *context->renderAssetLibrary;
			library.Init(context->assetDatabase);
			data = {{"reflectionParameters", camera->reflectionParameters}};
			data["cameraInstanceID"] = world->GetBindingComponentInstanceID(target, type);
			data["renderPassesAsset"] = ToString(camera->renderPasses);
			data["renderPassesRevision"] = library.GetRenderPassesRevision(camera->renderPasses);
			if (const auto* passes = ResolvePasses(*camera, library)) data["renderPasses"] = ToJson(*passes);
		} else if (type == 70) {
			const auto* component = world->TryGetComponentForBinding<SimpleAnimationComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 35) {
			const auto* component = world->TryGetComponentForBinding<UISelectableComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 71) {
			const auto* component = world->TryGetComponentForBinding<TimerComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 76) {
			if (!ReadTimelinePlayerSettings(*world, target, data)) return -1;
		} else if (type == 75) {
			const auto* component = world->TryGetComponentForBinding<DayNightCycleComponent>(target);
			if (!component) return -1;
			data = *component;
		} else if (type == 72) {
			if (!ReadMotionAnimationSettings(*world, target, data)) return -1;
		} else if (type == 81) {
			auto* component = world->TryGetComponentForBinding<TextCharAnimationComponent>(target);
			if (!component) return -1;
			data["sources"] = ReadTextSources(*world, target, GetCurrentContext(), *component);
		} else if (type == 8 || type == 9 || type == 26) {
			if (!ResolveComponentTimeSource(*world, target, type)) return -1;
			data = nlohmann::json::object();
		} else return -1;
		if (const auto* source = ResolveComponentTimeSource(*world, target, type)) {
			data["timeSource"] = *source;
			const auto reference = MakeNativeEntity(*world, ResolveTimeSourceEntity(*world, target, *source));
			data["timeSourceNative"] = WriteNativeReference(reference);
		}
		return CopyStringToBuffer(data.dump(), buffer, capacity);
	} catch (...) { return -1; }
}

int32_t ManagedScriptRuntime::ApplyComponentSettingsCallback(ManagedNativeEntity entity, int32_t type,
	const char* buffer, int32_t size) {

	if (!buffer || size <= 0 || size > 16 * 1024 * 1024) return 0;
	try {
		auto* world = ResolveWorld(entity);
		const auto target = ResolveEntity(entity);
		if (!world || !world->IsAlive(target)) return 0;
		const auto data = nlohmann::json::parse(buffer, buffer + size);
		if (type == 81 && data.contains("sourceSlot")) {
			if (!SetTextSource(*world, target, GetCurrentContext(), data)) return 0;
			world->MarkComponentModified(target, type);
			return 1;
		}
		if (type == 79) {
			auto* component = world->TryGetComponentForBinding<HexTilingComponent>(target);
			if (!component) return 0;
			const auto index = data.at("subMeshIndex").get<uint32_t>();
			const auto subMeshes = GetMeshSubMeshes(*world, target);
			if (subMeshes.size() <= index) return 0;
			const bool enabled = data.at("subMeshEnabled").get<bool>();
			if (IsHexTilingSubMeshEnabled(*component, subMeshes[index], index) == enabled) return 1;
			SetHexTilingSubMeshEnabled(*component, subMeshes[index], index, enabled);
			world->MarkComponentModified(target, type);
			return 1;
		}
		if (auto* settings = ResolveLightInfluenceSettings(*world, target, type)) {
			const auto operation = data.value("lightShadowExclusionPatch", std::string{});
			if (operation == "Clear") {
				if (settings->shadowExclusions.empty()) return 1;
				settings->shadowExclusions.clear();
			}
			else {
				if (operation != "Set" && operation != "Remove") return 0;
				const auto& value = data.at("targetNative");
				const auto reference = ReadNativeReference(value);
				const Entity excluded = ResolveEntity(reference);
				if (ResolveWorld(reference) != world || !world->IsAlive(excluded)) return 0;
				auto& values = settings->shadowExclusions;
				const auto matches = [&](const auto& entry) { return ResolveLightShadowExclusion(*world, target, entry) == excluded; };
				if (operation == "Remove") {
					if (std::erase_if(values, matches) == 0) return 1;
				}
				else {
					const bool includeChildren = data.value("includeChildren", false);
					auto found = std::find_if(values.begin(), values.end(), matches);
					if (found != values.end() && found->includeChildren == includeChildren) return 1;
					if (found == values.end()) found = values.insert(values.end(), LightShadowExclusion{});
					found->target = SceneEntityReference::FromEntity(*world, excluded);
					found->runtimeTargetID = world->GetUUID(excluded);
					found->includeChildren = includeChildren;
				}
			}
			world->MarkComponentModified(target, type);
			return 1;
		}
		auto restoreTimeSource = [&] {
			auto* source = ResolveComponentTimeSource(*world, target, type);
			if (!source || !data.contains("timeSourceNative") || source->mode != TimeSourceMode::Explicit) return;
			const auto& value = data.at("timeSourceNative");
			const auto reference = ReadNativeReference(value);
			if (!reference.IsValid() || ResolveWorld(reference) != world || !world->IsAlive(ResolveEntity(reference))) return;
			const auto persisted = SceneEntityReference::FromEntity(*world, ResolveEntity(reference));
			if (persisted.sourceAsset == source->source.sourceAsset && persisted.localFileID == source->source.localFileID)
				source->runtimeSourceID = world->GetUUID(ResolveEntity(reference));
		};
		if (data.value("renderSettingsPatch", false)) {
			auto* settings = ResolveRendererSettings(*world, target, type);
			if (!settings) return 0;
			auto candidate = *settings;
			from_json(data.at("renderSettings"), candidate);
			if (candidate != *settings) {
				*settings = candidate;
				world->MarkComponentModified(target, type);
			}
			return 1;
		}
		if (data.value("timeSourcePatch", false)) {
			auto* source = ResolveComponentTimeSource(*world, target, type);
			if (!source) return 0;
			auto candidate = data.at("timeSource").get<TimeSourceSettings>();
			const auto& value = data.at("timeSourceNative");
			ManagedNativeEntity reference{};
			reference.world.index = value.at("world").at("index").get<uint32_t>();
			reference.world.generation = value.at("world").at("generation").get<uint32_t>();
			reference.index = value.at("index").get<uint32_t>();
			reference.generation = value.at("generation").get<uint32_t>();
			if (reference.IsValid()) {
				if (ResolveWorld(reference) != world || !world->IsAlive(ResolveEntity(reference))) return 0;
				candidate.source = SceneEntityReference::FromEntity(*world, ResolveEntity(reference));
				candidate.runtimeSourceID = world->GetUUID(ResolveEntity(reference));
			} else candidate.source = {};
			*source = candidate;
			ClearLegacyUnscaledTime(*world, target, type);
			world->MarkComponentModified(target, type);
			return 1;
		}
		if (type == 77) {
			const auto* record = TryGetTrailRenderer(*world, target);
			if (!record) return 0;
			nlohmann::json settings = record->settings;
			settings.merge_patch(data);
			SetTrailRendererSettings(*world, target, settings.get<TrailRendererSettings>());
			return 1;
		}
		if (type == 78) {
			const auto* record = TryGetBeamRenderer(*world, target);
			if (!record) return 0;
			nlohmann::json settings = record->settings;
			settings.merge_patch(data);
			auto candidate = settings.get<BeamRendererSettings>();
			candidate.start.runtimeObject = record->settings.start.runtimeObject;
			candidate.end.runtimeObject = record->settings.end.runtimeObject;
			for (size_t i = 0; i < candidate.waypoints.size() && i < record->settings.waypoints.size(); ++i)
				candidate.waypoints[i].runtimeObject = record->settings.waypoints[i].runtimeObject;
			SetBeamRendererSettings(*world, target, candidate);
			return 1;
		}
		if (type == 76) {
			if (!ApplyTimelinePlayerSettings(*world, target, data)) return 0;
			restoreTimeSource();
			return 1;
		}
		if (type == 75) {
			auto* component = world->TryGetComponentForBinding<DayNightCycleComponent>(target);
			if (!component) return 0;
			from_json(data, *component);
			restoreTimeSource();
			world->MarkComponentModified<DayNightCycleComponent>(target);
			return 1;
		}
		if (type == 72) {
			if (!ApplyMotionAnimationSettings(*world, target, data)) return 0;
			restoreTimeSource();
			return 1;
		}
		if (type == 71) {
			auto* component = world->TryGetComponentForBinding<TimerComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			from_json(data, candidate);
			ApplyTimerSettings(candidate, *component);
			restoreTimeSource();
			world->MarkComponentModified<TimerComponent>(target);
			return 1;
		}
		if (type == 70) {
			auto* component = world->TryGetComponentForBinding<SimpleAnimationComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			if (data.contains("targetNative")) {
				const auto& value = data.at("targetNative");
				ManagedNativeEntity reference{};
				reference.world.index = value.at("world").at("index").get<uint32_t>();
				reference.world.generation = value.at("world").at("generation").get<uint32_t>();
				reference.index = value.at("index").get<uint32_t>();
				reference.generation = value.at("generation").get<uint32_t>();
				if (reference.IsValid()) {
					if (ResolveWorld(reference) != world || !world->IsAlive(ResolveEntity(reference))) return 0;
					candidate.runtimeTarget = ResolveEntity(reference);
					candidate.runtimeTargetOverride = true;
					candidate.target = SceneEntityReference::FromEntity(*world, candidate.runtimeTarget);
				} else {
					candidate.target = {}; candidate.runtimeTargetOverride = false;
				}
				candidate.binding = data.at("binding").get<AnimationPropertyBinding>();
			} else {
				from_json(data, candidate);
			}
			*component = std::move(candidate);
			restoreTimeSource();
			world->MarkComponentModified<SimpleAnimationComponent>(target);
			return 1;
		}
		if (type == 35) {
			auto* component = world->TryGetComponentForBinding<UISelectableComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			from_json(data, candidate);
			ApplyUISelectableAuthoring(candidate, *component);
			world->MarkComponentModified<UISelectableComponent>(target);
			return 1;
		}
		if (type == 0) {
			auto* component = world->TryGetComponentForBinding<AnimationClipPlayerComponent>(target);
			if (!component) return 0;
			auto candidate = *component;
			from_json(data, candidate);
			ApplyAnimationClipPlayerSettings(candidate, *component);
			restoreTimeSource();
			world->MarkComponentModified<AnimationClipPlayerComponent>(target);
			return 1;
		}
		if (type == 16 || type == 17) {
			auto* camera = ResolveCamera(*world, target, type);
			const auto* context = GetCurrentContext();
			if (!camera || !context || !context->renderAssetLibrary) return 0;
			// 別のCameraや再読込前のSnapshotを適用しない
			if (data.value("cameraInstanceID", uint64_t(0)) != world->GetBindingComponentInstanceID(target, type) ||
				ParseAssetID(data, "renderPassesAsset") != camera->renderPasses ||
				data.value("renderPassesRevision", uint64_t(0)) !=
				context->renderAssetLibrary->GetRenderPassesRevision(camera->renderPasses)) return 0;
			std::shared_ptr<const RenderPassesAsset> passes = camera->runtimeRenderPasses;
			if (data.contains("renderPasses")) {
				RenderPassesAsset candidate;
				if (!FromJson(data.at("renderPasses"), candidate)) return 0;
				std::unordered_set<uint64_t> ids;
				for (const auto& pass : candidate.passes) {
					if (!pass.id.value || !ids.insert(pass.id.value).second) return 0;
				}
				candidate.guid = camera->renderPasses;
				if (!candidate.guid) return 0;
				passes = std::make_shared<const RenderPassesAsset>(std::move(candidate));
			}
			if (data.contains("reflectionParameters") && !data["reflectionParameters"].is_object()) return 0;
			// 全項目の変換成功後にCamera固有の設定を公開する
			camera->runtimeRenderPasses = std::move(passes);
			camera->runtimeRenderPassesSourceRevision = context->renderAssetLibrary->GetRenderPassesRevision(camera->renderPasses);
			if (data.contains("reflectionParameters")) camera->reflectionParameters = data["reflectionParameters"];
			world->MarkComponentModified(target, type);
			return 1;
		}
		if (type == 14) {
			if (!world->HasComponent<MeshRendererComponent>(target)) return 0;
			const auto subMeshes = GetMeshSubMeshes(*world, target);
			const auto index = data.at("index").get<uint32_t>();
			if (index >= subMeshes.size()) return 0;
			SubMeshMaterial candidate = data.at("value").get<SubMeshMaterial>();
			if (candidate.stableID != subMeshes[index].stableID) return 0;
			// 読取専用のモデル由来情報は現在の値を維持する
			candidate.sourceSubMeshIndex = subMeshes[index].sourceSubMeshIndex;
			candidate.sourceSurfaceMode = subMeshes[index].sourceSurfaceMode;
			candidate.sourcePivot = subMeshes[index].sourcePivot;
			return SetMeshSubMesh(*world, target, index, candidate);
		}
		return 0;
	} catch (...) { return 0; }
}
