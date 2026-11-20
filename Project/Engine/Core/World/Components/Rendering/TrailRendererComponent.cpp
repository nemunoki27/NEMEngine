#include "TrailRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingStorageCleanup.h>

using namespace Engine;

namespace {

	bool SameReference(const SceneEntityReference& a, const SceneEntityReference& b) {

		return a.sourceAsset == b.sourceAsset && a.localFileID == b.localFileID && a.prefab == b.prefab;
	}

	void ApplySettings(TrailRendererRecord& record, TrailRendererSettings next) {

		const auto& previous = record.settings;
		const bool rootChanged = !SameReference(previous.root, next.root);
		const bool tipChanged = !SameReference(previous.tip, next.tip);
		if (previous.mode != next.mode || previous.is2D != next.is2D) record.runtime.points.clear();
		if (previous.emitting != next.emitting || rootChanged || tipChanged) record.runtime.breakBefore = true;
		if (rootChanged) record.runtime.rootOverride = Entity::Null();
		if (tipChanged) record.runtime.tipOverride = Entity::Null();
		PreserveTimeSourceRuntime(previous.timeSource, next.timeSource);
		next.lifetime = (std::max)(0.0f, next.lifetime);
		next.width = (std::max)(0.0f, next.width);
		next.widthMultiplier = (std::max)(0.0f, next.widthMultiplier);
		next.minVertexDistance = (std::max)(0.0f, next.minVertexDistance);
		record.settings = std::move(next);
	}

	TrailRendererRecord& EnsureTrail(ECSWorld& world, Entity entity, TrailRendererComponent& component) {

		auto& storage = world.GetStorage().Get<TrailRendererStorage>();
		auto* record = storage.TryGet(component.handle);
		const auto owner = world.GetUUID(entity);
		if (record && record->owner == owner) return *record;
		component.handle = storage.Emplace(TrailRendererRecord{record ? record->settings : TrailRendererSettings{}, {}, owner});
		return *storage.TryGet(component.handle);
	}
}

TrailRendererSettings::TrailRendererSettings() : material(BuiltinAssets::Materials::DefaultTrail) {

	widthCurve.channel.defaultValue = 1.0f;
	colorGradient.channels[3].AddKey(0.0f, 1.0f);
	colorGradient.channels[3].AddKey(1.0f, 0.0f);
}

void Engine::to_json(nlohmann::json& out, const TrailRendererSettings& s) {

	out = {{"mode", EnumAdapter<TrailMode>::ToString(s.mode)}, {"alignment", EnumAdapter<TrailAlignment>::ToString(s.alignment)},
		{"textureMode", EnumAdapter<TrailTextureMode>::ToString(s.textureMode)}, {"is2D", s.is2D}, {"emitting", s.emitting},
		{"lifetime", s.lifetime}, {"minVertexDistance", s.minVertexDistance}, {"width", s.width}, {"widthMultiplier", s.widthMultiplier},
		{"root", s.root}, {"tip", s.tip}, {"widthCurve", s.widthCurve.channel}, {"colorGradient", s.colorGradient.channels},
		{"timeSource", s.timeSource}, {"material", ToAssetReferenceJson(s.material)}, {"materialInstance", WriteMaterialInstance(s.materialInstance)},
		{"renderSettings", s.renderSettings}, {"renderingLayerMask", s.renderingLayerMask}};
	WriteRenderCommonFields(out, s.layer, s.order, s.visible, s.blendMode, s.queue);
}

void Engine::from_json(const nlohmann::json& in, TrailRendererSettings& s) {

	s.mode = EnumAdapter<TrailMode>::FromString(in.value("mode", "")).value_or(s.mode);
	s.alignment = EnumAdapter<TrailAlignment>::FromString(in.value("alignment", "")).value_or(s.alignment);
	s.textureMode = EnumAdapter<TrailTextureMode>::FromString(in.value("textureMode", "")).value_or(s.textureMode);
	s.is2D = in.value("is2D", s.is2D);
	s.emitting = in.value("emitting", s.emitting);
	s.lifetime = in.value("lifetime", s.lifetime);
	s.minVertexDistance = in.value("minVertexDistance", s.minVertexDistance);
	s.width = in.value("width", s.width);
	s.widthMultiplier = in.value("widthMultiplier", s.widthMultiplier);
	s.root = in.value("root", s.root);
	s.tip = in.value("tip", s.tip);
	s.timeSource = in.value("timeSource", s.timeSource);
	if (in.contains("widthCurve")) ReadCurveChannel(in.at("widthCurve"), s.widthCurve.channel, true);
	if (auto it = in.find("colorGradient"); it != in.end() && it->is_array() && it->size() == 4) {
		for (size_t i = 0; i < 4; ++i) ReadCurveChannel(it->at(i), s.colorGradient.channels[i], true);
	}
	if (in.contains("material")) s.material = ParseAssetID(in, "material");
	ReadMaterialInstance(in.value("materialInstance", nlohmann::json::object()), s.materialInstance);
	ReadRenderCommonFields(in, s.layer, s.order, s.visible, s.blendMode, s.queue);
	if (auto it = in.find("renderSettings"); it != in.end()) from_json(*it, s.renderSettings);
	s.renderingLayerMask = in.value("renderingLayerMask", s.renderingLayerMask) & kRenderingLayerMaskBits;
}

void TrailRendererComponent::InitializeStorage(ECSWorld& world, const Entity& owner, TrailRendererComponent& component) {

	EnsureTrail(world, owner, component);
}

void TrailRendererComponent::OnAdded(ECSWorld& world, const Entity& owner, TrailRendererComponent& component) {

	InitializeStorage(world, owner, component);
}

void TrailRendererComponent::OnRemoved(ECSWorld&, const Entity&) {}

void TrailRendererComponent::ReleaseStorage(ECSWorld& world, const Entity& owner, TrailRendererComponent& component) {

	auto& storage = world.GetStorage().Get<TrailRendererStorage>();
	const auto* record = storage.TryGet(component.handle);
	if (record && record->owner == world.GetUUID(owner)) storage.Release(component.handle);
	component.handle = {};
}

void TrailRendererComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& owner,
	const TrailRendererComponent& source, TrailRendererComponent& destination) {

	const auto* storage = sourceWorld.GetStorage().TryGet<TrailRendererStorage>();
	const auto* record = storage ? storage->TryGet(source.handle) : nullptr;
	destination.handle = destinationWorld.GetStorage().Get<TrailRendererStorage>().Emplace(
		TrailRendererRecord{record ? record->settings : TrailRendererSettings{}, {}, destinationWorld.GetUUID(owner)});
}

void TrailRendererComponent::DeserializeECS(ECSWorld& world, const Entity& owner, const nlohmann::json& in, TrailRendererComponent& component) {

	ApplySettings(EnsureTrail(world, owner, component), in.get<TrailRendererSettings>());
}

void TrailRendererComponent::SerializeECS(const ECSWorld& world, const Entity&, const TrailRendererComponent& component, nlohmann::json& out) {

	const auto* storage = world.GetStorage().TryGet<TrailRendererStorage>();
	const auto* record = storage ? storage->TryGet(component.handle) : nullptr;
	to_json(out, record ? record->settings : TrailRendererSettings{});
}

const TrailRendererRecord* Engine::TryGetTrailRenderer(const ECSWorld& world, Entity owner) {

	const auto* component = world.TryGetComponentForBinding<TrailRendererComponent>(owner);
	const auto* storage = world.GetStorage().TryGet<TrailRendererStorage>();
	return component && storage ? storage->TryGet(component->handle) : nullptr;
}

TrailRendererRecord* Engine::TryGetTrailRenderer(ECSWorld& world, Entity owner) {

	auto* component = world.TryGetComponentForBinding<TrailRendererComponent>(owner);
	if (!component) return nullptr;
	auto& record = EnsureTrail(world, owner, *component);
	TrackPendingStorage<TrailRendererComponent, TrailRendererStorage>(world, owner, &TrailRendererComponent::handle);
	return &record;
}

void Engine::SetTrailRendererSettings(ECSWorld& world, Entity owner, const TrailRendererSettings& settings) {

	if (auto* record = TryGetTrailRenderer(world, owner)) {
		ApplySettings(*record, settings);
		world.MarkComponentModified<TrailRendererComponent>(owner);
	}
}
