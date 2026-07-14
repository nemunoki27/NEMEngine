#include "ParticleEffectAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

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
	settings.blendMode = asset.blendMode;
	settings.queue = asset.queue;
	settings.billboardAxes = asset.billboardAxes;
	settings.trail = asset.trail;
	settings.emitter = asset.emitter;
	// フェーズごとのマテリアルと形状アニメの有無を集める
	settings.phaseMaterials.reserve(asset.phases.size());
	settings.phaseMaterialSettings.reserve(asset.phases.size());
	settings.trailPhaseSettings.reserve(asset.phases.size());
	for (const ParticleEffectPhase& phase : asset.phases) {

		settings.phaseMaterials.emplace_back(phase.material);
		settings.phaseMaterialSettings.emplace_back(phase.materialSettings);
		settings.trailPhaseSettings.emplace_back(MakeDefaultTrailPhaseSettings(asset.trail));
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
					entry.params, asset.trail.startColor, asset.trail.endColor);
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
		settings.trailPhaseSettings.emplace_back(MakeDefaultTrailPhaseSettings(asset.trail));
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
	outAsset.blendMode = EnumAdapter<BlendMode>::FromString(
		data.value("blendMode", "Add")).value_or(BlendMode::Add);
	outAsset.queue = RenderPhaseFromString(data.value("queue", "Transparent"), RenderPhase::Transparent);
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
		outAsset.trail.drawSource = it->value("drawSource", outAsset.trail.drawSource);
		outAsset.trail.keepAfterParticleDeath = it->value(
			"keepAfterParticleDeath", outAsset.trail.keepAfterParticleDeath);
		outAsset.trail.continueUpdateAfterParticleDeath = it->value(
			"continueUpdateAfterParticleDeath", outAsset.trail.continueUpdateAfterParticleDeath);
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
		if (outAsset.trail.keepAfterParticleDeath && outAsset.trail.pointLifetime <= 0.0f) {
			outAsset.trail.pointLifetime = ParticleTrailSettings::kDefaultPointLifetime;
		}
		outAsset.trail.material = ParseAssetID(*it, "material");
		if (const auto vit = it->find("materialSettings"); vit != it->end()) {
			from_json(*vit, outAsset.trail.materialSettings);
		}
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
				if (phase.parentSettings.useEmitter) {
					phase.parentSettings.entityLocalFileID = {};
				}
			}
			readModules(phaseJson, phase.modules);
			// 旧Phase直下のカスタムパラメータは専用モジュールへ移行する
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
	data["blendMode"] = EnumAdapter<BlendMode>::ToString(asset.blendMode);
	data["queue"] = std::string(ToString(asset.queue));
	data["billboardAxes"] = nlohmann::json::array();
	for (Axis axis : asset.billboardAxes) {
		data["billboardAxes"].push_back(EnumAdapter<Axis>::ToString(axis));
	}
	data["trail"] = nlohmann::json::object();
	data["trail"]["enabled"] = asset.trail.enabled;
	data["trail"]["drawSource"] = asset.trail.drawSource;
	data["trail"]["keepAfterParticleDeath"] = asset.trail.keepAfterParticleDeath;
	data["trail"]["continueUpdateAfterParticleDeath"] = asset.trail.continueUpdateAfterParticleDeath;
	data["trail"]["maxPoints"] = asset.trail.maxPoints;
	data["trail"]["minDistance"] = asset.trail.minDistance;
	data["trail"]["startWidth"] = asset.trail.startWidth;
	data["trail"]["endWidth"] = asset.trail.endWidth;
	data["trail"]["startColor"] = asset.trail.startColor.ToJson();
	data["trail"]["endColor"] = asset.trail.endColor.ToJson();
	data["trail"]["pointLifetime"] = asset.trail.pointLifetime;
	data["trail"]["material"] = ToAssetReferenceJson(asset.trail.material);
	to_json(data["trail"]["materialSettings"], asset.trail.materialSettings);

	data["phases"] = nlohmann::json::array();
	for (const ParticleEffectPhase& phase : asset.phases) {

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
		data["phases"].push_back(std::move(phaseJson));
	}
	return data;
}
