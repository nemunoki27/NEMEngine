#include "BeamRendererComponent.h"

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

	void PreserveEndpoint(const BeamEndpoint& previous, BeamEndpoint& next) {

		if (next.mode == BeamEndpointMode::Object && !next.runtimeObject.IsValid() && previous.mode == next.mode &&
			next.object.localFileID == previous.object.localFileID && next.object.sourceAsset == previous.object.sourceAsset && next.object.prefab == previous.object.prefab) {
			next.runtimeObject = previous.runtimeObject;
		}
	}

	void ApplySettings(BeamRendererRecord& record, BeamRendererSettings next, bool switchingDimension) {

		const auto& previous = record.settings;
		PreserveTimeSourceRuntime(previous.timeSource, next.timeSource);
		if (switchingDimension && previous.is2D != next.is2D && previous.end.mode == next.end.mode &&
			previous.end.position == next.end.position && next.end.mode == BeamEndpointMode::Local) {
			const Vector3 forward(0, 0, 3), right(3, 0, 0);
			if (next.is2D && next.end.position == forward) next.end.position = right;
			if (!next.is2D && next.end.position == right) next.end.position = forward;
		}
		next.subdivisions = std::clamp(next.subdivisions, 1, 256);
		next.branchCount = std::clamp(next.branchCount, 0, 64);
		next.frequency = std::clamp(next.frequency, 0.0f, 240.0f);
		next.amplitude = (std::max)(0.0f, next.amplitude);
		next.branchLength = (std::max)(0.0f, next.branchLength);
		next.branchWidth = (std::max)(0.0f, next.branchWidth);
		next.width = (std::max)(0.0f, next.width);
		next.widthMultiplier = (std::max)(0.0f, next.widthMultiplier);
		next.duration = (std::max)(0.0f, next.duration);
		record.settings = std::move(next);
	}

	BeamRendererSettings CloneSettings(const BeamRendererRecord* record) {

		if (!record) return {};
		auto settings = record->settings;
		settings.start.runtimeObject = settings.end.runtimeObject = Entity::Null();
		for (auto& point : settings.waypoints) point.runtimeObject = Entity::Null();
		settings.timeSource.runtimeSourceID = {};
		return settings;
	}

	BeamRendererRecord& EnsureBeam(ECSWorld& world, Entity entity, BeamRendererComponent& component) {

		auto& storage = world.GetStorage().Get<BeamRendererStorage>();
		auto* record = storage.TryGet(component.handle);
		const auto owner = world.GetUUID(entity);
		if (record && record->owner == owner) return *record;
		component.handle = storage.Emplace(BeamRendererRecord{CloneSettings(record), {}, owner});
		return *storage.TryGet(component.handle);
	}
}

BeamRendererSettings::BeamRendererSettings() : material(BuiltinAssets::Materials::DefaultBeam) {

	widthCurve.channel.defaultValue = 1.0f;
}

void Engine::to_json(nlohmann::json& out, const BeamEndpoint& p) {

	out = {{"mode", EnumAdapter<BeamEndpointMode>::ToString(p.mode)}, {"position", p.position.ToJson()}, {"object", p.object}};
}

void Engine::from_json(const nlohmann::json& in, BeamEndpoint& p) {

	p.mode = EnumAdapter<BeamEndpointMode>::FromString(in.value("mode", "")).value_or(p.mode);
	if (in.contains("position")) p.position = Vector3::FromJson(in.at("position"));
	p.object = in.value("object", p.object);
}

void Engine::to_json(nlohmann::json& out, const BeamRendererSettings& s) {

	out = {{"mode", EnumAdapter<BeamMode>::ToString(s.mode)}, {"playbackMode", EnumAdapter<BeamPlaybackMode>::ToString(s.playbackMode)},
		{"triggerInteraction", EnumAdapter<QueryTriggerInteraction>::ToString(s.triggerInteraction)},
		{"start", s.start}, {"end", s.end}, {"waypoints", s.waypoints},
		{"subdivisions", s.subdivisions}, {"branchCount", s.branchCount}, {"amplitude", s.amplitude}, {"frequency", s.frequency},
		{"branchLength", s.branchLength}, {"branchWidth", s.branchWidth}, {"seed", s.seed},
		{"duration", s.duration}, {"playOnStart", s.playOnStart}, {"stopOnHit", s.stopOnHit}, {"collisionMask", s.collisionMask},
		{"useWidthCurve", s.useWidthCurve}, {"is2D", s.is2D},
		{"width", s.width}, {"widthMultiplier", s.widthMultiplier},
		{"widthCurve", s.widthCurve.channel},
		{"timeSource", s.timeSource}, {"material", ToAssetReferenceJson(s.material)}, {"materialInstance", WriteMaterialInstance(s.materialInstance)},
		{"renderSettings", s.renderSettings}, {"renderingLayerMask", s.renderingLayerMask}};
	WriteRenderCommonFields(out, s.layer, s.order, s.visible, s.blendMode, s.queue);
}

void Engine::from_json(const nlohmann::json& in, BeamRendererSettings& s) {

	s.mode = EnumAdapter<BeamMode>::FromString(in.value("mode", "")).value_or(s.mode);
	s.playbackMode = EnumAdapter<BeamPlaybackMode>::FromString(in.value("playbackMode", "")).value_or(s.playbackMode);
	s.triggerInteraction = EnumAdapter<QueryTriggerInteraction>::FromString(in.value("triggerInteraction", "")).value_or(s.triggerInteraction);
	s.useWidthCurve = in.value("useWidthCurve", in.contains("widthCurve"));
	s.is2D = in.value("is2D", s.is2D);
	s.start = in.value("start", s.start);
	if (s.is2D && !in.contains("end")) s.end.position = {3, 0, 0};
	s.end = in.value("end", s.end);
	s.waypoints = in.value("waypoints", s.waypoints);
	s.subdivisions = in.value("subdivisions", s.subdivisions);
	s.branchCount = in.value("branchCount", s.branchCount);
	s.amplitude = in.value("amplitude", s.amplitude);
	s.frequency = in.value("frequency", s.frequency);
	s.branchLength = in.value("branchLength", s.branchLength);
	s.branchWidth = in.value("branchWidth", s.branchWidth);
	s.seed = in.value("seed", s.seed);
	s.duration = in.value("duration", s.duration);
	s.playOnStart = in.value("playOnStart", s.playOnStart);
	s.stopOnHit = in.value("stopOnHit", s.stopOnHit);
	s.collisionMask = in.value("collisionMask", s.collisionMask);
	s.width = in.value("width", s.width);
	s.widthMultiplier = in.value("widthMultiplier", s.widthMultiplier);
	s.timeSource = in.value("timeSource", s.timeSource);
	if (in.contains("widthCurve")) ReadCurveChannel(in.at("widthCurve"), s.widthCurve.channel, true);
	if (in.contains("material")) s.material = ParseAssetID(in, "material");
	ReadMaterialInstance(in.value("materialInstance", nlohmann::json::object()), s.materialInstance);
	if (!s.material || (s.material == BuiltinAssets::Materials::DefaultTrail && in.contains("colorGradient"))) s.material = BuiltinAssets::Materials::DefaultBeam;
	if (s.material == BuiltinAssets::Materials::DefaultBeam) {
		if (auto it = in.find("colorGradient"); it != in.end() && it->is_array() && it->size() == 4) {
			CurveColor4 gradient;
			for (size_t i = 0; i < 4; ++i) ReadCurveChannel(it->at(i), gradient.channels[i], true);
			for (const auto& [name, t] : {std::pair{"startColor", 0.0f}, std::pair{"targetColor", 1.0f}}) {
				if (!s.materialInstance.Find(MaterialParameterID::FromName(name))) {
					s.materialInstance.Set(name, MaterialParameterValue{.value = gradient.Evaluate(t)});
				}
			}
		}
	}
	ReadRenderCommonFields(in, s.layer, s.order, s.visible, s.blendMode, s.queue);
	if (auto it = in.find("renderSettings"); it != in.end()) from_json(*it, s.renderSettings);
	s.renderingLayerMask = in.value("renderingLayerMask", s.renderingLayerMask) & kRenderingLayerMaskBits;
}

void BeamRendererComponent::InitializeStorage(ECSWorld& world, const Entity& owner, BeamRendererComponent& component) {

	EnsureBeam(world, owner, component);
}

void BeamRendererComponent::OnAdded(ECSWorld& world, const Entity& owner, BeamRendererComponent& component) {

	InitializeStorage(world, owner, component);
}

void BeamRendererComponent::OnRemoved(ECSWorld&, const Entity&) {}

void BeamRendererComponent::ReleaseStorage(ECSWorld& world, const Entity& owner, BeamRendererComponent& component) {

	auto& storage = world.GetStorage().Get<BeamRendererStorage>();
	const auto* record = storage.TryGet(component.handle);
	if (record && record->owner == world.GetUUID(owner)) storage.Release(component.handle);
	component.handle = {};
}

void BeamRendererComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& owner,
	const BeamRendererComponent& source, BeamRendererComponent& destination) {

	const auto* storage = sourceWorld.GetStorage().TryGet<BeamRendererStorage>();
	const auto* record = storage ? storage->TryGet(source.handle) : nullptr;
	destination.handle = destinationWorld.GetStorage().Get<BeamRendererStorage>().Emplace(
		BeamRendererRecord{CloneSettings(record), {}, destinationWorld.GetUUID(owner)});
}

void BeamRendererComponent::DeserializeECS(ECSWorld& world, const Entity& owner, const nlohmann::json& in, BeamRendererComponent& component) {

	auto& record = EnsureBeam(world, owner, component);
	auto next = in.get<BeamRendererSettings>();
	PreserveEndpoint(record.settings.start, next.start);
	PreserveEndpoint(record.settings.end, next.end);
	if (next.waypoints.size() == record.settings.waypoints.size()) {
		for (size_t i = 0; i < next.waypoints.size(); ++i) PreserveEndpoint(record.settings.waypoints[i], next.waypoints[i]);
	}
	ApplySettings(record, std::move(next), false);
}

void BeamRendererComponent::SerializeECS(const ECSWorld& world, const Entity&, const BeamRendererComponent& component, nlohmann::json& out) {

	const auto* storage = world.GetStorage().TryGet<BeamRendererStorage>();
	const auto* record = storage ? storage->TryGet(component.handle) : nullptr;
	to_json(out, record ? record->settings : BeamRendererSettings{});
}

const BeamRendererRecord* Engine::TryGetBeamRenderer(const ECSWorld& world, Entity owner) {

	const auto* component = world.TryGetComponentForBinding<BeamRendererComponent>(owner);
	const auto* storage = world.GetStorage().TryGet<BeamRendererStorage>();
	return component && storage ? storage->TryGet(component->handle) : nullptr;
}

BeamRendererRecord* Engine::TryGetBeamRenderer(ECSWorld& world, Entity owner) {

	auto* component = world.TryGetComponentForBinding<BeamRendererComponent>(owner);
	if (!component) return nullptr;
	auto& record = EnsureBeam(world, owner, *component);
	TrackPendingStorage<BeamRendererComponent, BeamRendererStorage>(world, owner, &BeamRendererComponent::handle);
	return &record;
}

void Engine::SetBeamRendererSettings(ECSWorld& world, Entity owner, const BeamRendererSettings& settings) {

	if (auto* record = TryGetBeamRenderer(world, owner)) {
		ApplySettings(*record, settings, true);
		world.MarkComponentModified<BeamRendererComponent>(owner);
	}
}
