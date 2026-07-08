#include "ParticleEffectAsset.h"

//============================================================================
//	include
//============================================================================
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
	for (const ParticleEffectModuleEntry& entry : asset.modules) {
		if (entry.id == "ShapeOverLifetime") {
			settings.shapeOverLifetime = true;
			break;
		}
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
		if (const auto vit = e.find("lifetime"); vit != e.end()) { from_json(*vit, emitter.lifetime); }
		if (const auto vit = e.find("speed"); vit != e.end()) { from_json(*vit, emitter.speed); }
		emitter.sphereRadius = e.value("sphereRadius", emitter.sphereRadius);
		if (const auto vit = e.find("boxSize"); vit != e.end()) { emitter.boxSize = Vector3::FromJson(*vit); }
		emitter.boxFacePosX = e.value("boxFacePosX", emitter.boxFacePosX);
		emitter.boxFaceNegX = e.value("boxFaceNegX", emitter.boxFaceNegX);
		emitter.boxFacePosY = e.value("boxFacePosY", emitter.boxFacePosY);
		emitter.boxFaceNegY = e.value("boxFaceNegY", emitter.boxFaceNegY);
		emitter.boxFacePosZ = e.value("boxFacePosZ", emitter.boxFacePosZ);
		emitter.boxFaceNegZ = e.value("boxFaceNegZ", emitter.boxFaceNegZ);
		emitter.torusRadius = e.value("torusRadius", emitter.torusRadius);
		emitter.torusThickness = e.value("torusThickness", emitter.torusThickness);
		emitter.circleRadius = e.value("circleRadius", emitter.circleRadius);
		emitter.circleArc = e.value("circleArc", emitter.circleArc);
		emitter.coneAngle = e.value("coneAngle", emitter.coneAngle);
		emitter.coneRadius = e.value("coneRadius", emitter.coneRadius);
		if (const auto vit = e.find("pointDirection"); vit != e.end()) { emitter.pointDirection = Vector3::FromJson(*vit); }
		if (const auto vit = e.find("rectSize"); vit != e.end()) { emitter.rectSize = Vector2::FromJson(*vit); }
		emitter.rectEdgePosX = e.value("rectEdgePosX", emitter.rectEdgePosX);
		emitter.rectEdgeNegX = e.value("rectEdgeNegX", emitter.rectEdgeNegX);
		emitter.rectEdgePosY = e.value("rectEdgePosY", emitter.rectEdgePosY);
		emitter.rectEdgeNegY = e.value("rectEdgeNegY", emitter.rectEdgeNegY);
	}
	// 描画空間に合わない形状はフォールバックする
	if (outAsset.space == PrimitiveRenderSpace::Screen2D) {
		if (!IsParticleEmitterShape2D(outAsset.emitter.shape)) {
			outAsset.emitter.shape = ParticleEmitterShape::Circle;
		}
	} else if (outAsset.emitter.shape == ParticleEmitterShape::Rect ||
		outAsset.emitter.shape == ParticleEmitterShape::Cone2D) {
		outAsset.emitter.shape = ParticleEmitterShape::Sphere;
	}

	outAsset.space = EnumAdapter<PrimitiveRenderSpace>::FromString(
		data.value("space", "World3D")).value_or(PrimitiveRenderSpace::World3D);
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
		outAsset.trail.width = it->value("width", outAsset.trail.width);
	}

	// 未知のモジュールは読み飛ばして他のモジュールの再生を継続する
	if (data.contains("modules") && data["modules"].is_array()) {
		for (const auto& moduleJson : data["modules"]) {

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
			outAsset.modules.emplace_back(std::move(entry));
		}
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
		to_json(e["lifetime"], emitter.lifetime);
		to_json(e["speed"], emitter.speed);
		e["sphereRadius"] = emitter.sphereRadius;
		e["boxSize"] = emitter.boxSize.ToJson();
		e["boxFacePosX"] = emitter.boxFacePosX;
		e["boxFaceNegX"] = emitter.boxFaceNegX;
		e["boxFacePosY"] = emitter.boxFacePosY;
		e["boxFaceNegY"] = emitter.boxFaceNegY;
		e["boxFacePosZ"] = emitter.boxFacePosZ;
		e["boxFaceNegZ"] = emitter.boxFaceNegZ;
		e["torusRadius"] = emitter.torusRadius;
		e["torusThickness"] = emitter.torusThickness;
		e["circleRadius"] = emitter.circleRadius;
		e["circleArc"] = emitter.circleArc;
		e["coneAngle"] = emitter.coneAngle;
		e["coneRadius"] = emitter.coneRadius;
		e["pointDirection"] = emitter.pointDirection.ToJson();
		e["rectSize"] = emitter.rectSize.ToJson();
		e["rectEdgePosX"] = emitter.rectEdgePosX;
		e["rectEdgeNegX"] = emitter.rectEdgeNegX;
		e["rectEdgePosY"] = emitter.rectEdgePosY;
		e["rectEdgeNegY"] = emitter.rectEdgeNegY;
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
	data["trail"]["width"] = asset.trail.width;

	data["modules"] = nlohmann::json::array();
	for (const ParticleEffectModuleEntry& entry : asset.modules) {

		nlohmann::json moduleJson = nlohmann::json::object();
		moduleJson["id"] = entry.id;
		moduleJson["params"] = entry.params;
		data["modules"].push_back(std::move(moduleJson));
	}
	return data;
}
