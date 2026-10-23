#include "ParticleSystemSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

// c++
#include <algorithm>
#include <unordered_set>
#include <utility>

using namespace Engine;

namespace {

	// 保存形式と識別子を検証して設定を読み込む
	bool ReadSettings(const nlohmann::json& groupJson, ParticleSystemSettings& group) {

		if (!groupJson.is_object() || groupJson.value("version", 0) != 1) return false;
		// 発生と描画の設定を読み込む
		group = ParticleSystemSettings{};
		group.space = EnumAdapter<PrimitiveRenderSpace>::FromString(groupJson.value("space", "World3D")).value_or(PrimitiveRenderSpace::World3D);
		group.looping = groupJson.value("looping", group.looping);
		if (const auto it = groupJson.find("emitter"); it != groupJson.end() && it->is_object()) {

			const nlohmann::json& e = *it;
			ParticleEmitterSettings& emitter = group.emitter;
			emitter.shape = EnumAdapter<ParticleEmitterShape>::FromString(e.value("shape", "Sphere"))
								.value_or(ParticleEmitterShape::Sphere);
			emitter.emitInterval = e.value("emitInterval", emitter.emitInterval);
			if (const auto vit = e.find("emitCount"); vit != e.end()) {
				from_json(*vit, emitter.emitCount);
			}
			emitter.maxParticles = e.value("maxParticles", emitter.maxParticles);
			if (const auto vit = e.find("speed"); vit != e.end()) {
				from_json(*vit, emitter.speed);
			}
			if (const auto vit = e.find("emitOffset"); vit != e.end()) {
				from_json(*vit, emitter.emitOffset);
			}
			for (const auto& [shape, instance] : ParticleEmitterShapeRegistry::GetInstance().GetMap()) {
				instance->FromJson(e, emitter);
			}
		}
		// 描画空間に対応するEmitter形状へ補正する
		const IParticleEmitterShape* emitterShape =
			ParticleEmitterShapeRegistry::GetInstance().Find(group.emitter.shape);
		if (group.space == PrimitiveRenderSpace::Screen2D) {
			if (!emitterShape || !emitterShape->Supports2D()) {
				group.emitter.shape = ParticleEmitterShape::Circle;
			}
		} else if (!emitterShape || !emitterShape->Supports3D()) {
			group.emitter.shape = ParticleEmitterShape::Sphere;
		}

		// 粒子の形状と描画設定を読み込む
		group.shape =
			EnumAdapter<PrimitiveType>::FromString(groupJson.value("shape", "Plane")).value_or(PrimitiveType::Plane);
		if (group.space == PrimitiveRenderSpace::Screen2D && group.shape != PrimitiveType::Plane &&
			group.shape != PrimitiveType::Ring) {
			group.shape = PrimitiveType::Plane;
		}
		if (const auto it = groupJson.find("plane"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.plane);
		}
		if (const auto it = groupJson.find("crossPlane"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.crossPlane);
		}
		if (const auto it = groupJson.find("ring"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.ring);
		}
		if (const auto it = groupJson.find("cylinder"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.cylinder);
		}
		if (const auto it = groupJson.find("sphere"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.sphere);
		}
		if (const auto it = groupJson.find("hemisphere"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.hemisphere);
		}
		if (const auto it = groupJson.find("cube"); it != groupJson.end() && it->is_object()) {
			from_json(*it, group.cube);
		}
		group.model = ParseAssetID(groupJson, "model");
		group.material = ParseAssetID(groupJson, "material");
		group.blendMode =
			EnumAdapter<BlendMode>::FromString(groupJson.value("blendMode", "Add")).value_or(BlendMode::Add);
		group.queue = RenderPhaseFromString(groupJson.value("queue", "Transparent"), RenderPhase::Transparent);
		group.renderingLayerMask =
			groupJson.value("renderingLayerMask", group.renderingLayerMask) & kRenderingLayerMaskBits;
		if (const auto it = groupJson.find("billboardAxes"); it != groupJson.end() && it->is_array()) {

			group.billboardAxes.clear();
			for (const auto& axisJson : *it) {
				if (const auto axis = EnumAdapter<Axis>::FromString(axisJson.get<std::string>())) {
					group.billboardAxes.emplace_back(*axis);
				}
			}
		}
		if (const auto it = groupJson.find("trail"); it != groupJson.end() && it->is_object()) {

			// Trailの保持期間とMaterialを読み込む
			group.trail.enabled = it->value("enabled", group.trail.enabled);
			group.trail.drawSource = it->value("drawSource", group.trail.drawSource);
			group.trail.keepAfterParticleDeath =
				it->value("keepAfterParticleDeath", group.trail.keepAfterParticleDeath);
			group.trail.mode = EnumAdapter<ParticleTrailMode>::FromString(it->value("mode", "Particle")).value_or(ParticleTrailMode::Particle);
			group.trail.textureMode = EnumAdapter<ParticleTrailTextureMode>::FromString(it->value("textureMode", "Stretch")).value_or(ParticleTrailTextureMode::Stretch);
			group.trail.tileLength = (std::max)(it->value("tileLength", group.trail.tileLength), 0.001f);
			group.trail.maxTrails = std::clamp(it->value("maxTrails", group.trail.maxTrails), 1u, 1000000u);
			group.trail.memoryBudgetMB = std::clamp(it->value("memoryBudgetMB", group.trail.memoryBudgetMB), 1u, 256u);
			group.trail.maxPoints = std::clamp(it->value("maxPoints", group.trail.maxPoints), 2, 4096);
			group.trail.minDistance = it->value("minDistance", group.trail.minDistance);
			group.trail.startWidth = it->value("startWidth", group.trail.startWidth);
			group.trail.endWidth = it->value("endWidth", group.trail.endWidth);
			if (const auto vit = it->find("startColor"); vit != it->end()) {
				group.trail.startColor = Color4::FromJson(*vit);
			}
			if (const auto vit = it->find("endColor"); vit != it->end()) {
				group.trail.endColor = Color4::FromJson(*vit);
			}
			group.trail.pointLifetime = (std::max)(it->value("pointLifetime", group.trail.pointLifetime), 0.001f);
			if (group.trail.keepAfterParticleDeath && group.trail.pointLifetime <= 0.0f) {
				group.trail.pointLifetime = ParticleTrailSettings::kDefaultPointLifetime;
			}
			group.trail.material = ParseAssetID(*it, "material");
			if (const auto vit = it->find("materialSettings"); vit != it->end()) {
				from_json(*vit, group.trail.materialSettings);
			}
		}

		const auto readModules = [](const nlohmann::json& parent, std::vector<ParticleModuleEntry>& outModules) {
			// Moduleの識別子とパラメータを読み込む
			if (!parent.contains("modules") || !parent["modules"].is_array()) {
				return;
			}
			for (const auto& moduleJson : parent["modules"]) {

				if (!moduleJson.is_object()) {
					continue;
				}
				ParticleModuleEntry entry{};
				entry.id = moduleJson.value("id", "");
				if (moduleJson.contains("instanceID")) {
					entry.instanceID = FromString16Hex(moduleJson.at("instanceID").get<std::string>());
				}
				if (entry.id.empty()) {
					continue;
				}
				if (const auto it = moduleJson.find("params"); it != moduleJson.end() && it->is_object()) {
					entry.params = *it;
				}
				outModules.emplace_back(std::move(entry));
			}
		};

		if (const auto it = groupJson.find("phases"); it != groupJson.end() && it->is_array()) {
			// Phaseごとの寿命と親設定を読み込む
			for (const auto& phaseJson : *it) {

				if (!phaseJson.is_object()) {
					continue;
				}
				ParticlePhaseSettings phase{};
				if (phaseJson.contains("id")) {
					phase.id = FromString16Hex(phaseJson.at("id").get<std::string>());
				}
				phase.name = phaseJson.value("name", phase.name);
				if (const auto vit = phaseJson.find("lifetime"); vit != phaseJson.end()) {
					from_json(*vit, phase.lifetime);
				}
				phase.lifeEndMode = EnumAdapter<ParticleLifeEndMode>::FromString(phaseJson.value("lifeEndMode", "Kill"))
										.value_or(ParticleLifeEndMode::Kill);
				phase.material = ParseAssetID(phaseJson, "material");
				if (const auto mit = phaseJson.find("materialSettings"); mit != phaseJson.end()) {
					from_json(*mit, phase.materialSettings);
				}
				if (const auto pit = phaseJson.find("parentSettings"); pit != phaseJson.end() && pit->is_object()) {

					phase.parentSettings.useEmitter = pit->value("useEmitter", false);
					phase.parentSettings.ignoreParentRotation = pit->value("ignoreParentRotation", false);
					phase.parentSettings.ignoreParentScale =
						pit->value("ignoreParentScale", phase.parentSettings.ignoreParentScale);
					phase.parentSettings.keepWorldOnDetach = pit->value("keepWorldOnDetach", true);
				}
				readModules(phaseJson, phase.modules);
				group.phases.emplace_back(std::move(phase));
			}
		}
		if (group.phases.empty()) {
			group.phases.emplace_back();
		}
		std::unordered_set<Engine::UUID> identifiers;
		for (const auto& phase : group.phases) {
			if (!phase.id || !identifiers.insert(phase.id).second) return false;
			for (const auto& module : phase.modules) {
				if (!module.instanceID || !identifiers.insert(module.instanceID).second) return false;
			}
		}
		return true;
	}
}

ParticleSystemSettings Engine::MakeDefaultParticleSystemSettings() {

	ParticleSystemSettings settings;
	settings.phases.emplace_back();
	settings.phases.front().modules.push_back({"SizeOverLifetime", {{"startScale", 1.0f}, {"endScale", 0.0f}}});
	settings.phases.front().modules.push_back({"ColorOverLifetime", nlohmann::json::object()});
	return settings;
}

bool Engine::FromJson(const nlohmann::json& data, ParticleSystemSettings& settings) {

	// 読込失敗で現在の設定を失わないよう候補を使う
	ParticleSystemSettings candidate;
	if (!ReadSettings(data, candidate)) return false;
	settings = std::move(candidate);
	return true;
}

nlohmann::json Engine::ToJson(const ParticleSystemSettings& group) {

	nlohmann::json groupJson = nlohmann::json::object();
	groupJson["version"] = 1;
	groupJson["space"] = EnumAdapter<PrimitiveRenderSpace>::ToString(group.space);
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

	// 共通の形状と描画設定を書き出す
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
	groupJson["renderingLayerMask"] = group.renderingLayerMask & kRenderingLayerMaskBits;
	groupJson["billboardAxes"] = nlohmann::json::array();
	for (Axis axis : group.billboardAxes) {
		groupJson["billboardAxes"].push_back(EnumAdapter<Axis>::ToString(axis));
	}
	groupJson["trail"] = nlohmann::json::object();
	groupJson["trail"]["enabled"] = group.trail.enabled;
	groupJson["trail"]["drawSource"] = group.trail.drawSource;
	groupJson["trail"]["keepAfterParticleDeath"] = group.trail.keepAfterParticleDeath;
	groupJson["trail"]["mode"] = EnumAdapter<ParticleTrailMode>::ToString(group.trail.mode);
	groupJson["trail"]["textureMode"] = EnumAdapter<ParticleTrailTextureMode>::ToString(group.trail.textureMode);
	groupJson["trail"]["tileLength"] = group.trail.tileLength;
	groupJson["trail"]["maxTrails"] = group.trail.maxTrails;
	groupJson["trail"]["memoryBudgetMB"] = group.trail.memoryBudgetMB;
	groupJson["trail"]["maxPoints"] = group.trail.maxPoints;
	groupJson["trail"]["minDistance"] = group.trail.minDistance;
	groupJson["trail"]["startWidth"] = group.trail.startWidth;
	groupJson["trail"]["endWidth"] = group.trail.endWidth;
	groupJson["trail"]["startColor"] = group.trail.startColor.ToJson();
	groupJson["trail"]["endColor"] = group.trail.endColor.ToJson();
	groupJson["trail"]["pointLifetime"] = group.trail.pointLifetime;
	groupJson["trail"]["material"] = ToAssetReferenceJson(group.trail.material);
	to_json(groupJson["trail"]["materialSettings"], group.trail.materialSettings);

	// PhaseとModuleの設定を順番どおりに保存する
	groupJson["phases"] = nlohmann::json::array();
	for (const ParticlePhaseSettings& phase : group.phases) {

		nlohmann::json phaseJson = nlohmann::json::object();
		phaseJson["name"] = phase.name;
		phaseJson["id"] = ToString(phase.id);
		to_json(phaseJson["lifetime"], phase.lifetime);
		phaseJson["lifeEndMode"] = EnumAdapter<ParticleLifeEndMode>::ToString(phase.lifeEndMode);
		phaseJson["material"] = ToAssetReferenceJson(phase.material);
		to_json(phaseJson["materialSettings"], phase.materialSettings);
		phaseJson["parentSettings"] = nlohmann::json::object();
		phaseJson["parentSettings"]["useEmitter"] = phase.parentSettings.useEmitter;
		phaseJson["parentSettings"]["ignoreParentRotation"] = phase.parentSettings.ignoreParentRotation;
		phaseJson["parentSettings"]["ignoreParentScale"] = phase.parentSettings.ignoreParentScale;
		phaseJson["parentSettings"]["keepWorldOnDetach"] = phase.parentSettings.keepWorldOnDetach;
		phaseJson["modules"] = nlohmann::json::array();
		for (const ParticleModuleEntry& entry : phase.modules) {

			nlohmann::json moduleJson = nlohmann::json::object();
			moduleJson["id"] = entry.id;
			moduleJson["instanceID"] = ToString(entry.instanceID);
			moduleJson["params"] = entry.params;
			phaseJson["modules"].push_back(std::move(moduleJson));
		}
		groupJson["phases"].push_back(std::move(phaseJson));
	}
	return groupJson;
}
