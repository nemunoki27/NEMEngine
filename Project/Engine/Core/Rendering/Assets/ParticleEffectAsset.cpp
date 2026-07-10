#include "ParticleEffectAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	ParticleEffectAsset classMethods
//============================================================================
Engine::ParticleRenderSettings Engine::MakeParticleRenderSettings(const ParticleEffectAsset& asset) {

	ParticleRenderSettings settings{};
	settings.space = asset.space;
	settings.shape = asset.shape;
	settings.plane = asset.plane;
	settings.crossPlane = asset.crossPlane;
	settings.ring = asset.ring;
	settings.cylinder = asset.cylinder;
	settings.sphere = asset.sphere;
	settings.hemisphere = asset.hemisphere;
	settings.cube = asset.cube;
	settings.model = asset.model;
	settings.material = asset.material;
	settings.sortMode = asset.sortMode;
	settings.billboardAxes = asset.billboardAxes;
	settings.trail = asset.trail;
	settings.emitter = asset.emitter;
	// フェーズごとのマテリアルと形状アニメの有無を集める
	settings.phaseMaterials.reserve(asset.phases.size());
	for (const ParticleEffectPhase& phase : asset.phases) {

		settings.phaseMaterials.emplace_back(phase.material);
		for (const ParticleEffectModuleEntry& entry : phase.modules) {
			if (entry.id == "ShapeOverLifetime") {
				settings.shapeOverLifetime = true;
			}
		}
	}
	if (settings.phaseMaterials.empty()) {
		settings.phaseMaterials.emplace_back();
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
	outAsset.version = data.value("version", outAsset.version);

	outAsset.duration = data.value("duration", outAsset.duration);
	outAsset.looping = data.value("looping", outAsset.looping);
	// 旧スキーマの上限はエミッター設定へ引き継ぐ
	outAsset.emitter.maxParticles = data.value("maxParticles", outAsset.emitter.maxParticles);
	if (const auto it = data.find("emitter"); it != data.end() && it->is_object()) {

		const nlohmann::json& e = *it;
		ParticleEmitterSettings& emitter = outAsset.emitter;
		emitter.shape = EnumAdapter<ParticleEmitterShape>::FromString(
			e.value("shape", "Sphere")).value_or(ParticleEmitterShape::Sphere);
		emitter.emitInterval = e.value("emitInterval", emitter.emitInterval);
		if (const auto vit = e.find("emitCount"); vit != e.end()) { from_json(*vit, emitter.emitCount); }
		emitter.maxParticles = e.value("maxParticles", emitter.maxParticles);
		if (const auto vit = e.find("speed"); vit != e.end()) { from_json(*vit, emitter.speed); }
		if (const auto vit = e.find("emitOffset"); vit != e.end()) { from_json(*vit, emitter.emitOffset); }
		// 形状パラメータは各形状が自分の分を読む
		for (const auto& [shape, instance] : ParticleEmitterShapeRegistry::GetInstance().GetMap()) {
			instance->FromJson(e, emitter);
		}
	}
	outAsset.space = EnumAdapter<PrimitiveRenderSpace>::FromString(
		data.value("space", "World3D")).value_or(PrimitiveRenderSpace::World3D);
	// 描画空間に合わない発生形状はフォールバックする
	{
		const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(outAsset.emitter.shape);
		if (outAsset.space == PrimitiveRenderSpace::Screen2D) {
			if (!shape || !shape->Supports2D()) {
				outAsset.emitter.shape = ParticleEmitterShape::Circle;
			}
		} else if (!shape || !shape->Supports3D()) {
			outAsset.emitter.shape = ParticleEmitterShape::Sphere;
		}
	}
	outAsset.shape = EnumAdapter<PrimitiveType>::FromString(
		data.value("shape", "Plane")).value_or(PrimitiveType::Plane);
	// 2DはPlane/Ringのみ対応、他形状はPlaneへ落とす
	if (outAsset.space == PrimitiveRenderSpace::Screen2D &&
		outAsset.shape != PrimitiveType::Plane && outAsset.shape != PrimitiveType::Ring) {
		outAsset.shape = PrimitiveType::Plane;
	}
	if (const auto it = data.find("plane"); it != data.end() && it->is_object()) { from_json(*it, outAsset.plane); }
	if (const auto it = data.find("crossPlane"); it != data.end() && it->is_object()) { from_json(*it, outAsset.crossPlane); }
	if (const auto it = data.find("ring"); it != data.end() && it->is_object()) { from_json(*it, outAsset.ring); }
	if (const auto it = data.find("cylinder"); it != data.end() && it->is_object()) { from_json(*it, outAsset.cylinder); }
	if (const auto it = data.find("sphere"); it != data.end() && it->is_object()) { from_json(*it, outAsset.sphere); }
	if (const auto it = data.find("hemisphere"); it != data.end() && it->is_object()) { from_json(*it, outAsset.hemisphere); }
	if (const auto it = data.find("cube"); it != data.end() && it->is_object()) { from_json(*it, outAsset.cube); }
	outAsset.model = ParseAssetID(data, "model");

	outAsset.material = ParseAssetID(data, "material");
	outAsset.sortMode = EnumAdapter<ParticleSortMode>::FromString(
		data.value("sortMode", "None")).value_or(ParticleSortMode::None);
	if (const auto it = data.find("billboardAxes"); it != data.end() && it->is_array()) {

		outAsset.billboardAxes.clear();
		for (const auto& axisJson : *it) {
			if (const auto axis = EnumAdapter<Axis>::FromString(axisJson.get<std::string>())) {
				outAsset.billboardAxes.emplace_back(*axis);
			}
		}
	}
	if (const auto it = data.find("trail"); it != data.end() && it->is_object()) {

		outAsset.trail.enabled = it->value("enabled", outAsset.trail.enabled);
		outAsset.trail.maxPoints = it->value("maxPoints", outAsset.trail.maxPoints);
		outAsset.trail.minDistance = it->value("minDistance", outAsset.trail.minDistance);
		// 旧スキーマの単一幅は両端へ引き継ぐ
		if (const auto vit = it->find("width"); vit != it->end()) {

			outAsset.trail.startWidth = vit->get<float>();
			outAsset.trail.endWidth = vit->get<float>();
		}
		outAsset.trail.startWidth = it->value("startWidth", outAsset.trail.startWidth);
		outAsset.trail.endWidth = it->value("endWidth", outAsset.trail.endWidth);
		if (const auto vit = it->find("startColor"); vit != it->end()) { outAsset.trail.startColor = Color4::FromJson(*vit); }
		if (const auto vit = it->find("endColor"); vit != it->end()) { outAsset.trail.endColor = Color4::FromJson(*vit); }
		outAsset.trail.pointLifetime = it->value("pointLifetime", outAsset.trail.pointLifetime);
		outAsset.trail.material = ParseAssetID(*it, "material");
	}

	// モジュール配列を読み込む、未知のモジュールは読み飛ばして他のモジュールの再生を継続する
	const auto readModules = [](const nlohmann::json& parent, std::vector<ParticleEffectModuleEntry>& outModules) {
		if (!parent.contains("modules") || !parent["modules"].is_array()) {
			return;
		}
		for (const auto& moduleJson : parent["modules"]) {

			if (!moduleJson.is_object()) {
				continue;
			}
			ParticleEffectModuleEntry entry{};
			entry.id = moduleJson.value("id", "");
			if (entry.id.empty()) {
				continue;
			}
			if (const auto it = moduleJson.find("params"); it != moduleJson.end() && it->is_object()) {
				entry.params = *it;
			}
			outModules.emplace_back(std::move(entry));
		}
		};

	// フェーズを読み込む、旧スキーマはトップレベルのモジュールとエミッターの寿命を1フェーズへ移行する
	if (const auto it = data.find("phases"); it != data.end() && it->is_array()) {
		for (const auto& phaseJson : *it) {

			if (!phaseJson.is_object()) {
				continue;
			}
			ParticleEffectPhase phase{};
			phase.name = phaseJson.value("name", phase.name);
			if (const auto vit = phaseJson.find("lifetime"); vit != phaseJson.end()) { from_json(*vit, phase.lifetime); }
			phase.lifeEndMode = EnumAdapter<ParticleLifeEndMode>::FromString(
				phaseJson.value("lifeEndMode", "Kill")).value_or(ParticleLifeEndMode::Kill);
			phase.material = ParseAssetID(phaseJson, "material");
			readModules(phaseJson, phase.modules);
			outAsset.phases.emplace_back(std::move(phase));
		}
	} else {

		ParticleEffectPhase phase{};
		if (const auto eit = data.find("emitter"); eit != data.end() && eit->is_object()) {
			if (const auto vit = eit->find("lifetime"); vit != eit->end()) { from_json(*vit, phase.lifetime); }
		}
		readModules(data, phase.modules);
		outAsset.phases.emplace_back(std::move(phase));
	}
	// フェーズは必ず1つ以上持つ
	if (outAsset.phases.empty()) {
		outAsset.phases.emplace_back();
	}
	return true;
}

nlohmann::json Engine::ToJson(const ParticleEffectAsset& asset) {

	nlohmann::json data = nlohmann::json::object();
	data["name"] = asset.name;
	data["version"] = asset.version;

	data["duration"] = asset.duration;
	data["looping"] = asset.looping;
	{
		nlohmann::json e = nlohmann::json::object();
		const ParticleEmitterSettings& emitter = asset.emitter;
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
		data["emitter"] = std::move(e);
	}

	data["space"] = EnumAdapter<PrimitiveRenderSpace>::ToString(asset.space);
	data["shape"] = EnumAdapter<PrimitiveType>::ToString(asset.shape);
	data["plane"] = asset.plane;
	data["crossPlane"] = asset.crossPlane;
	data["ring"] = asset.ring;
	data["cylinder"] = asset.cylinder;
	data["sphere"] = asset.sphere;
	data["hemisphere"] = asset.hemisphere;
	data["cube"] = asset.cube;
	data["model"] = ToAssetReferenceJson(asset.model);

	data["material"] = ToAssetReferenceJson(asset.material);
	data["sortMode"] = EnumAdapter<ParticleSortMode>::ToString(asset.sortMode);
	data["billboardAxes"] = nlohmann::json::array();
	for (Axis axis : asset.billboardAxes) {
		data["billboardAxes"].push_back(EnumAdapter<Axis>::ToString(axis));
	}
	data["trail"] = nlohmann::json::object();
	data["trail"]["enabled"] = asset.trail.enabled;
	data["trail"]["maxPoints"] = asset.trail.maxPoints;
	data["trail"]["minDistance"] = asset.trail.minDistance;
	data["trail"]["startWidth"] = asset.trail.startWidth;
	data["trail"]["endWidth"] = asset.trail.endWidth;
	data["trail"]["startColor"] = asset.trail.startColor.ToJson();
	data["trail"]["endColor"] = asset.trail.endColor.ToJson();
	data["trail"]["pointLifetime"] = asset.trail.pointLifetime;
	data["trail"]["material"] = ToAssetReferenceJson(asset.trail.material);

	data["phases"] = nlohmann::json::array();
	for (const ParticleEffectPhase& phase : asset.phases) {

		nlohmann::json phaseJson = nlohmann::json::object();
		phaseJson["name"] = phase.name;
		to_json(phaseJson["lifetime"], phase.lifetime);
		phaseJson["lifeEndMode"] = EnumAdapter<ParticleLifeEndMode>::ToString(phase.lifeEndMode);
		phaseJson["material"] = ToAssetReferenceJson(phase.material);
		phaseJson["modules"] = nlohmann::json::array();
		for (const ParticleEffectModuleEntry& entry : phase.modules) {

			nlohmann::json moduleJson = nlohmann::json::object();
			moduleJson["id"] = entry.id;
			moduleJson["params"] = entry.params;
			phaseJson["modules"].push_back(std::move(moduleJson));
		}
		data["phases"].push_back(std::move(phaseJson));
	}
	return data;
}
