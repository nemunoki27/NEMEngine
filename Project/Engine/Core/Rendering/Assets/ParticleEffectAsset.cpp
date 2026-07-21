#include "ParticleEffectAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <unordered_set>

//============================================================================
//	ParticleEffectAsset internal
//============================================================================
namespace {

	Engine::Vector4 ToVector4(const Engine::Color4& color) {

		return Engine::Vector4(color.r, color.g, color.b, color.a);
	}

	Engine::ParticleMaterialAnimatedParameter MakeTrailFloatAnimation(
		const nlohmann::json& params, const char* startKey, const char* endKey,
		float defaultStart, float defaultEnd) {

		Engine::ParticleMaterialAnimatedParameter animation{};
		animation.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		animation.start.x = params.value(startKey, defaultStart);
		animation.end.x = params.value(endKey, defaultEnd);
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		animation.componentCount = 1;
		animation.useCurve = params.value("useCurve", false);
		if (const auto it = params.find("curve"); it != params.end() && it->is_object()) {
			from_json(*it, animation.curveW.channel);
		}
		if (const auto it = params.find("loop"); it != params.end()) {
			from_json(*it, animation.loop);
		}
		return animation;
	}

	Engine::ParticleMaterialAnimatedParameter MakeTrailColorAnimation(const nlohmann::json& params,
		const Engine::Color4& defaultStart, const Engine::Color4& defaultEnd) {

		Engine::ParticleMaterialAnimatedParameter animation{};
		animation.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		animation.start = ToVector4(defaultStart);
		animation.end = ToVector4(defaultEnd);
		if (const auto it = params.find("startColor"); it != params.end()) {
			animation.start = ToVector4(Engine::Color4::FromJson(*it));
		}
		if (const auto it = params.find("endColor"); it != params.end()) {
			animation.end = ToVector4(Engine::Color4::FromJson(*it));
		}
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		animation.componentCount = 4;
		animation.useCurve = params.value("useCurve", false);
		if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

			const size_t xyzCount = (std::min)(animation.curve3.channels.size(), it->size());
			for (size_t i = 0; i < xyzCount; ++i) {
				from_json((*it)[i], animation.curve3.channels[i]);
			}
			if (3 < it->size()) {
				from_json((*it)[3], animation.curveW.channel);
			}
		}
		if (const auto it = params.find("loop"); it != params.end()) {
			from_json(*it, animation.loop);
		}
		return animation;
	}

	Engine::ParticleMaterialAnimatedParameter MakeTrailVector2Animation(
		const nlohmann::json& params, const Engine::Vector2& defaultValue) {

		Engine::ParticleMaterialAnimatedParameter animation{};
		animation.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		Engine::Vector2 start = defaultValue;
		Engine::Vector2 end = defaultValue;
		if (const auto it = params.find("start"); it != params.end()) { start = Engine::Vector2::FromJson(*it); }
		if (const auto it = params.find("end"); it != params.end()) { end = Engine::Vector2::FromJson(*it); }
		animation.start = Engine::Vector4(start.x, start.y, 0.0f, 0.0f);
		animation.end = Engine::Vector4(end.x, end.y, 0.0f, 0.0f);
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		animation.componentCount = 2;
		animation.useCurve = params.value("useCurve", false);
		if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

			const size_t count = (std::min)(static_cast<size_t>(2), it->size());
			for (size_t i = 0; i < count; ++i) {
				from_json((*it)[i], animation.curve3.channels[i]);
			}
		}
		if (const auto it = params.find("loop"); it != params.end()) {
			from_json(*it, animation.loop);
		}
		return animation;
	}

	Engine::ParticleMaterialAnimatedParameter MakeTrailRotationAnimation(const nlohmann::json& params) {

		Engine::ParticleMaterialAnimatedParameter animation{};
		animation.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		animation.start.x = params.value("start", 0.0f);
		animation.end.x = params.value("end", 0.0f);
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		animation.componentCount = 1;
		animation.useCurve = params.value("useCurve", false);
		if (const auto it = params.find("curve"); it != params.end() && it->is_object()) {
			from_json(*it, animation.curveW.channel);
		}
		if (const auto it = params.find("loop"); it != params.end()) {
			from_json(*it, animation.loop);
		}
		return animation;
	}

	Engine::ParticleTrailPhaseSettings MakeDefaultTrailPhaseSettings(
		const Engine::ParticleTrailSettings& trail) {

		Engine::ParticleTrailPhaseSettings settings{};
		settings.width.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		settings.width.start.x = trail.startWidth;
		settings.width.end.x = trail.endWidth;
		settings.width.componentCount = 1;
		settings.color.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		settings.color.start = ToVector4(trail.startColor);
		settings.color.end = ToVector4(trail.endColor);
		settings.color.componentCount = 4;
		settings.uv.offset.constant = Engine::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		settings.uv.offset.componentCount = 2;
		settings.uv.scale.constant = Engine::Vector4(1.0f, 1.0f, 0.0f, 0.0f);
		settings.uv.scale.componentCount = 2;
		settings.uv.rotation.componentCount = 1;
		return settings;
	}
}

//============================================================================
//	ParticleEffectAsset classMethods
//============================================================================
Engine::ParticleRenderSettings Engine::MakeParticleRenderSettings(
	PrimitiveRenderSpace space, const ParticleEffectGroup& group) {

	ParticleRenderSettings settings{};
	settings.space = space;
	settings.shape = group.shape;
	settings.plane = group.plane;
	settings.crossPlane = group.crossPlane;
	settings.ring = group.ring;
	settings.cylinder = group.cylinder;
	settings.sphere = group.sphere;
	settings.hemisphere = group.hemisphere;
	settings.cube = group.cube;
	settings.model = group.model;
	settings.material = group.material;
	settings.blendMode = group.blendMode;
	settings.queue = group.queue;
	settings.billboardAxes = group.billboardAxes;
	settings.trail = group.trail;
	settings.emitter = group.emitter;
	// フェーズごとのマテリアルと形状アニメの有無を集める
	settings.phaseMaterials.reserve(group.phases.size());
	settings.phaseMaterialSettings.reserve(group.phases.size());
	settings.trailPhaseSettings.reserve(group.phases.size());
	for (const ParticleEffectPhase& phase : group.phases) {

		settings.phaseMaterials.emplace_back(phase.material);
		settings.phaseMaterialSettings.emplace_back(phase.materialSettings);
		settings.trailPhaseSettings.emplace_back(MakeDefaultTrailPhaseSettings(group.trail));
		for (const ParticleEffectModuleEntry& entry : phase.modules) {
			if (entry.id == "ShapeOverLifetime") {
				settings.shapeOverLifetime = true;
			}
			if (entry.id == "CustomShaderParameter") {

				const auto parameters = entry.params.find("parameters");
				if (parameters == entry.params.end() || !parameters->is_object()) {
					continue;
				}
				for (auto parameter = parameters->begin(); parameter != parameters->end(); ++parameter) {

					ParticleMaterialAnimatedParameter value{};
					from_json(parameter.value(), value);
					settings.phaseMaterialSettings.back().parameters[parameter.key()] = std::move(value);
				}
			}
			if (entry.id == "TrailSizeOverLifetime") {
				settings.trailPhaseSettings.back().width = MakeTrailFloatAnimation(
					entry.params, "startScale", "endScale", 0.1f, 0.0f);
			}
			if (entry.id == "TrailColorOverLifetime") {
				settings.trailPhaseSettings.back().color = MakeTrailColorAnimation(
					entry.params, group.trail.startColor, group.trail.endColor);
			}
			if (entry.id == "TrailColorUV") {

				ParticleTrailUVAnimationSettings& uv = settings.trailPhaseSettings.back().uv;
				if (const auto it = entry.params.find("offset"); it != entry.params.end() && it->is_object()) {
					uv.offset = MakeTrailVector2Animation(*it, Vector2::AnyInit(0.0f));
					uv.scroll = it->value("updateType", "Lerp") == "Scroll";
					if (const auto value = it->find("scrollSpeed"); value != it->end()) {
						uv.scrollSpeed = Vector2::FromJson(*value);
					}
				}
				if (const auto it = entry.params.find("scale"); it != entry.params.end() && it->is_object()) {
					uv.scale = MakeTrailVector2Animation(*it, Vector2::AnyInit(1.0f));
				}
				if (const auto it = entry.params.find("rotation"); it != entry.params.end() && it->is_object()) {
					uv.rotation = MakeTrailRotationAnimation(*it);
					if (const auto value = it->find("pivot"); value != it->end()) {
						uv.pivot = Vector2::FromJson(*value);
					}
				}
			}
			if (entry.id == "TrailCustomShaderParameter") {

				const auto parameters = entry.params.find("parameters");
				if (parameters == entry.params.end() || !parameters->is_object()) {
					continue;
				}
				for (auto parameter = parameters->begin(); parameter != parameters->end(); ++parameter) {

					ParticleMaterialAnimatedParameter value{};
					from_json(parameter.value(), value);
					settings.trailPhaseSettings.back().parameters[parameter.key()] = std::move(value);
				}
			}
		}
	}
	if (settings.phaseMaterials.empty()) {
		settings.phaseMaterials.emplace_back();
		settings.phaseMaterialSettings.emplace_back();
		settings.trailPhaseSettings.emplace_back(MakeDefaultTrailPhaseSettings(group.trail));
	}
	return settings;
}

bool Engine::FromJson(const nlohmann::json& data, ParticleEffectAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = ParticleEffectAsset{};
	outAsset.guid = ParseAssetID(data, "guid");
	outAsset.name = data.value("name", "UnnamedEffect");
	outAsset.version = 2;
	outAsset.space = EnumAdapter<PrimitiveRenderSpace>::FromString(
		data.value("space", "World3D")).value_or(PrimitiveRenderSpace::World3D);
	if (const auto it = data.find("groupEmission"); it != data.end() && it->is_object()) {

		outAsset.groupEmission.mode = EnumAdapter<ParticleEffectGroupEmissionMode>::FromString(
			it->value("mode", "Independent")).value_or(ParticleEffectGroupEmissionMode::Independent);
		outAsset.groupEmission.interval = it->value("interval", outAsset.groupEmission.interval);
		outAsset.groupEmission.waitForCompletion = it->value(
			"waitForCompletion", outAsset.groupEmission.waitForCompletion);
	}

	const auto readGroup = [&](const nlohmann::json& groupJson, ParticleEffectGroup& group) {

		group = ParticleEffectGroup{};
		if (const auto parsed = TryParseUUID16Hex(groupJson.value("id", std::string{}))) {
			group.id = *parsed;
		}
		group.name = groupJson.value("name", group.name);
		group.enabled = groupJson.value("enabled", group.enabled);
		group.looping = groupJson.value("looping", group.looping);
		group.emitter.maxParticles = groupJson.value("maxParticles", group.emitter.maxParticles);
		if (const auto it = groupJson.find("emitter"); it != groupJson.end() && it->is_object()) {

			const nlohmann::json& e = *it;
			ParticleEmitterSettings& emitter = group.emitter;
			emitter.shape = EnumAdapter<ParticleEmitterShape>::FromString(
				e.value("shape", "Sphere")).value_or(ParticleEmitterShape::Sphere);
			emitter.emitInterval = e.value("emitInterval", emitter.emitInterval);
			if (const auto vit = e.find("emitCount"); vit != e.end()) { from_json(*vit, emitter.emitCount); }
			emitter.maxParticles = e.value("maxParticles", emitter.maxParticles);
			if (const auto vit = e.find("speed"); vit != e.end()) { from_json(*vit, emitter.speed); }
			if (const auto vit = e.find("emitOffset"); vit != e.end()) { from_json(*vit, emitter.emitOffset); }
			for (const auto& [shape, instance] : ParticleEmitterShapeRegistry::GetInstance().GetMap()) {
				instance->FromJson(e, emitter);
			}
		}
		const IParticleEmitterShape* emitterShape =
			ParticleEmitterShapeRegistry::GetInstance().Find(group.emitter.shape);
		if (outAsset.space == PrimitiveRenderSpace::Screen2D) {
			if (!emitterShape || !emitterShape->Supports2D()) { group.emitter.shape = ParticleEmitterShape::Circle; }
		} else if (!emitterShape || !emitterShape->Supports3D()) {
			group.emitter.shape = ParticleEmitterShape::Sphere;
		}

		group.shape = EnumAdapter<PrimitiveType>::FromString(
			groupJson.value("shape", "Plane")).value_or(PrimitiveType::Plane);
		if (outAsset.space == PrimitiveRenderSpace::Screen2D &&
			group.shape != PrimitiveType::Plane && group.shape != PrimitiveType::Ring) {
			group.shape = PrimitiveType::Plane;
		}
		if (const auto it = groupJson.find("plane"); it != groupJson.end() && it->is_object()) { from_json(*it, group.plane); }
		if (const auto it = groupJson.find("crossPlane"); it != groupJson.end() && it->is_object()) { from_json(*it, group.crossPlane); }
		if (const auto it = groupJson.find("ring"); it != groupJson.end() && it->is_object()) { from_json(*it, group.ring); }
		if (const auto it = groupJson.find("cylinder"); it != groupJson.end() && it->is_object()) { from_json(*it, group.cylinder); }
		if (const auto it = groupJson.find("sphere"); it != groupJson.end() && it->is_object()) { from_json(*it, group.sphere); }
		if (const auto it = groupJson.find("hemisphere"); it != groupJson.end() && it->is_object()) { from_json(*it, group.hemisphere); }
		if (const auto it = groupJson.find("cube"); it != groupJson.end() && it->is_object()) { from_json(*it, group.cube); }
		group.model = ParseAssetID(groupJson, "model");
		group.material = ParseAssetID(groupJson, "material");
		group.blendMode = EnumAdapter<BlendMode>::FromString(
			groupJson.value("blendMode", "Add")).value_or(BlendMode::Add);
		group.queue = RenderPhaseFromString(groupJson.value("queue", "Transparent"), RenderPhase::Transparent);
		if (const auto it = groupJson.find("billboardAxes"); it != groupJson.end() && it->is_array()) {

			group.billboardAxes.clear();
			for (const auto& axisJson : *it) {
				if (const auto axis = EnumAdapter<Axis>::FromString(axisJson.get<std::string>())) {
					group.billboardAxes.emplace_back(*axis);
				}
			}
		}
		if (const auto it = groupJson.find("trail"); it != groupJson.end() && it->is_object()) {

			group.trail.enabled = it->value("enabled", group.trail.enabled);
			group.trail.drawSource = it->value("drawSource", group.trail.drawSource);
			group.trail.keepAfterParticleDeath = it->value(
				"keepAfterParticleDeath", group.trail.keepAfterParticleDeath);
			group.trail.continueUpdateAfterParticleDeath = it->value(
				"continueUpdateAfterParticleDeath", group.trail.continueUpdateAfterParticleDeath);
			group.trail.maxPoints = it->value("maxPoints", group.trail.maxPoints);
			group.trail.minDistance = it->value("minDistance", group.trail.minDistance);
			if (const auto vit = it->find("width"); vit != it->end()) {
				group.trail.startWidth = vit->get<float>();
				group.trail.endWidth = vit->get<float>();
			}
			group.trail.startWidth = it->value("startWidth", group.trail.startWidth);
			group.trail.endWidth = it->value("endWidth", group.trail.endWidth);
			if (const auto vit = it->find("startColor"); vit != it->end()) { group.trail.startColor = Color4::FromJson(*vit); }
			if (const auto vit = it->find("endColor"); vit != it->end()) { group.trail.endColor = Color4::FromJson(*vit); }
			group.trail.pointLifetime = it->value("pointLifetime", group.trail.pointLifetime);
			if (group.trail.keepAfterParticleDeath && group.trail.pointLifetime <= 0.0f) {
				group.trail.pointLifetime = ParticleTrailSettings::kDefaultPointLifetime;
			}
			group.trail.material = ParseAssetID(*it, "material");
			if (const auto vit = it->find("materialSettings"); vit != it->end()) {
				from_json(*vit, group.trail.materialSettings);
			}
		}

		const auto readModules = [](const nlohmann::json& parent,
			std::vector<ParticleEffectModuleEntry>& outModules) {

			if (!parent.contains("modules") || !parent["modules"].is_array()) { return; }
			for (const auto& moduleJson : parent["modules"]) {

				if (!moduleJson.is_object()) { continue; }
				ParticleEffectModuleEntry entry{};
				entry.id = moduleJson.value("id", "");
				if (entry.id.empty()) { continue; }
				if (entry.id == "RotationOverLifetime") { entry.id = "Rotation"; }
				if (const auto it = moduleJson.find("params"); it != moduleJson.end() && it->is_object()) {
					entry.params = *it;
				}
				outModules.emplace_back(std::move(entry));
			}
			};

		if (const auto it = groupJson.find("phases"); it != groupJson.end() && it->is_array()) {
			for (const auto& phaseJson : *it) {

				if (!phaseJson.is_object()) { continue; }
				ParticleEffectPhase phase{};
				phase.name = phaseJson.value("name", phase.name);
				if (const auto vit = phaseJson.find("lifetime"); vit != phaseJson.end()) { from_json(*vit, phase.lifetime); }
				phase.lifeEndMode = EnumAdapter<ParticleLifeEndMode>::FromString(
					phaseJson.value("lifeEndMode", "Kill")).value_or(ParticleLifeEndMode::Kill);
				phase.material = ParseAssetID(phaseJson, "material");
				if (const auto mit = phaseJson.find("materialSettings"); mit != phaseJson.end()) {
					from_json(*mit, phase.materialSettings);
				}
				if (const auto pit = phaseJson.find("parentSettings"); pit != phaseJson.end() && pit->is_object()) {

					phase.parentSettings.useEmitter = pit->value("useEmitter", false);
					phase.parentSettings.ignoreParentRotation = pit->value("ignoreParentRotation", false);
					phase.parentSettings.ignoreParentScale = pit->value("ignoreParentScale", false);
					phase.parentSettings.keepWorldOnDetach = pit->value("keepWorldOnDetach", true);
					const std::string localFileID = pit->value("entityLocalFileID", "");
					phase.parentSettings.entityLocalFileID = localFileID.empty() ? UUID{} : FromString16Hex(localFileID);
					if (phase.parentSettings.useEmitter) { phase.parentSettings.entityLocalFileID = {}; }
				}
				readModules(phaseJson, phase.modules);
				if (const auto settings = phaseJson.find("materialSettings");
					settings != phaseJson.end() && settings->is_object()) {

					if (const auto parameters = settings->find("parameters");
						parameters != settings->end() && parameters->is_object() && !parameters->empty()) {

						ParticleEffectModuleEntry entry{};
						entry.id = "CustomShaderParameter";
						entry.params["parameters"] = *parameters;
						phase.modules.emplace_back(std::move(entry));
					}
				}
				group.phases.emplace_back(std::move(phase));
			}
		} else {

			ParticleEffectPhase phase{};
			if (const auto eit = groupJson.find("emitter"); eit != groupJson.end() && eit->is_object()) {
				if (const auto vit = eit->find("lifetime"); vit != eit->end()) { from_json(*vit, phase.lifetime); }
			}
			readModules(groupJson, phase.modules);
			group.phases.emplace_back(std::move(phase));
		}
		if (group.phases.empty()) { group.phases.emplace_back(); }
		};

	if (const auto it = data.find("groups"); it != data.end() && it->is_array()) {
		for (const auto& groupJson : *it) {

			if (!groupJson.is_object()) { continue; }
			ParticleEffectGroup group{};
			readGroup(groupJson, group);
			outAsset.groups.emplace_back(std::move(group));
		}
	} else {

		ParticleEffectGroup group{};
		readGroup(data, group);
		group.id = UUID{ 1 };
		group.name = outAsset.name.empty() ? "Group 1" : outAsset.name;
		outAsset.groups.emplace_back(std::move(group));
	}
	if (outAsset.groups.empty()) { outAsset.groups.emplace_back(); }
	std::unordered_set<UUID> groupIDs{};
	for (ParticleEffectGroup& group : outAsset.groups) {

		while (!group.id || groupIDs.contains(group.id)) { group.id = UUID::New(); }
		groupIDs.emplace(group.id);
	}
	return true;
}

nlohmann::json Engine::ToJson(const ParticleEffectAsset& asset) {

	nlohmann::json data = nlohmann::json::object();
	data["name"] = asset.name;
	data["version"] = 2;
	data["space"] = EnumAdapter<PrimitiveRenderSpace>::ToString(asset.space);
	data["groupEmission"] = {
		{ "mode", EnumAdapter<ParticleEffectGroupEmissionMode>::ToString(asset.groupEmission.mode) },
		{ "interval", asset.groupEmission.interval },
		{ "waitForCompletion", asset.groupEmission.waitForCompletion },
	};
	data["groups"] = nlohmann::json::array();
	for (const ParticleEffectGroup& group : asset.groups) {

		nlohmann::json groupJson = nlohmann::json::object();
		groupJson["id"] = ToString(group.id);
		groupJson["name"] = group.name;
		groupJson["enabled"] = group.enabled;
		groupJson["looping"] = group.looping;
		{
			nlohmann::json e = nlohmann::json::object();
			const ParticleEmitterSettings& emitter = group.emitter;
			e["shape"] = EnumAdapter<ParticleEmitterShape>::ToString(emitter.shape);
			e["emitInterval"] = emitter.emitInterval;
			to_json(e["emitCount"], emitter.emitCount);
			e["maxParticles"] = emitter.maxParticles;
			to_json(e["speed"], emitter.speed);
			to_json(e["emitOffset"], emitter.emitOffset);
			// 形状パラメータは各形状が自分の分を書く
			for (const auto& [shape, instance] : ParticleEmitterShapeRegistry::GetInstance().GetMap()) {
				instance->ToJson(e, emitter);
			}
			groupJson["emitter"] = std::move(e);
		}

		groupJson["shape"] = EnumAdapter<PrimitiveType>::ToString(group.shape);
		groupJson["plane"] = group.plane;
		groupJson["crossPlane"] = group.crossPlane;
		groupJson["ring"] = group.ring;
		groupJson["cylinder"] = group.cylinder;
		groupJson["sphere"] = group.sphere;
		groupJson["hemisphere"] = group.hemisphere;
		groupJson["cube"] = group.cube;
		groupJson["model"] = ToAssetReferenceJson(group.model);
		groupJson["material"] = ToAssetReferenceJson(group.material);
		groupJson["blendMode"] = EnumAdapter<BlendMode>::ToString(group.blendMode);
		groupJson["queue"] = std::string(ToString(group.queue));
		groupJson["billboardAxes"] = nlohmann::json::array();
		for (Axis axis : group.billboardAxes) {
			groupJson["billboardAxes"].push_back(EnumAdapter<Axis>::ToString(axis));
		}
		groupJson["trail"] = nlohmann::json::object();
		groupJson["trail"]["enabled"] = group.trail.enabled;
		groupJson["trail"]["drawSource"] = group.trail.drawSource;
		groupJson["trail"]["keepAfterParticleDeath"] = group.trail.keepAfterParticleDeath;
		groupJson["trail"]["continueUpdateAfterParticleDeath"] = group.trail.continueUpdateAfterParticleDeath;
		groupJson["trail"]["maxPoints"] = group.trail.maxPoints;
		groupJson["trail"]["minDistance"] = group.trail.minDistance;
		groupJson["trail"]["startWidth"] = group.trail.startWidth;
		groupJson["trail"]["endWidth"] = group.trail.endWidth;
		groupJson["trail"]["startColor"] = group.trail.startColor.ToJson();
		groupJson["trail"]["endColor"] = group.trail.endColor.ToJson();
		groupJson["trail"]["pointLifetime"] = group.trail.pointLifetime;
		groupJson["trail"]["material"] = ToAssetReferenceJson(group.trail.material);
		to_json(groupJson["trail"]["materialSettings"], group.trail.materialSettings);

		groupJson["phases"] = nlohmann::json::array();
		for (const ParticleEffectPhase& phase : group.phases) {

			nlohmann::json phaseJson = nlohmann::json::object();
			phaseJson["name"] = phase.name;
			to_json(phaseJson["lifetime"], phase.lifetime);
			phaseJson["lifeEndMode"] = EnumAdapter<ParticleLifeEndMode>::ToString(phase.lifeEndMode);
			phaseJson["material"] = ToAssetReferenceJson(phase.material);
			to_json(phaseJson["materialSettings"], phase.materialSettings);
			phaseJson["parentSettings"] = nlohmann::json::object();
			phaseJson["parentSettings"]["useEmitter"] = phase.parentSettings.useEmitter;
			phaseJson["parentSettings"]["entityLocalFileID"] = phase.parentSettings.entityLocalFileID ?
				ToString(phase.parentSettings.entityLocalFileID) : "";
			phaseJson["parentSettings"]["ignoreParentRotation"] = phase.parentSettings.ignoreParentRotation;
			phaseJson["parentSettings"]["ignoreParentScale"] = phase.parentSettings.ignoreParentScale;
			phaseJson["parentSettings"]["keepWorldOnDetach"] = phase.parentSettings.keepWorldOnDetach;
			phaseJson["modules"] = nlohmann::json::array();
			for (const ParticleEffectModuleEntry& entry : phase.modules) {

				nlohmann::json moduleJson = nlohmann::json::object();
				moduleJson["id"] = entry.id;
				moduleJson["params"] = entry.params;
				phaseJson["modules"].push_back(std::move(moduleJson));
			}
			groupJson["phases"].push_back(std::move(phaseJson));
		}
		data["groups"].push_back(std::move(groupJson));
	}
	return data;
}
