#include "ParticleEffectAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

// c++
#include <algorithm>
#include <utility>

namespace {

	Engine::Vector4 ToVector4(const Engine::Color4& color) {

		// 色をShader Parameterの成分へ変換する
		return Engine::Vector4(color.r, color.g, color.b, color.a);
	}

	Engine::ParticleMaterialAnimatedParameter MakeTrailFloatAnimation(
		const nlohmann::json& params, const char* startKey, const char* endKey, float defaultStart, float defaultEnd) {

		// 1成分の寿命アニメーションを作る
		Engine::ParticleMaterialAnimatedParameter animation{};
		animation.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		animation.start.x = params.value(startKey, defaultStart);
		animation.end.x = params.value(endKey, defaultEnd);
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(params.value("easingType", "EaseOutSine"))
								   .value_or(EasingType::EaseOutSine);
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

	Engine::ParticleMaterialAnimatedParameter MakeTrailColorAnimation(
		const nlohmann::json& params, const Engine::Color4& defaultStart, const Engine::Color4& defaultEnd) {

		// 色の開始値と終了値を4成分へ展開する
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
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(params.value("easingType", "EaseOutSine"))
								   .value_or(EasingType::EaseOutSine);
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

		// UVの開始値と終了値を2成分へ展開する
		Engine::ParticleMaterialAnimatedParameter animation{};
		animation.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		Engine::Vector2 start = defaultValue;
		Engine::Vector2 end = defaultValue;
		if (const auto it = params.find("start"); it != params.end()) {
			start = Engine::Vector2::FromJson(*it);
		}
		if (const auto it = params.find("end"); it != params.end()) {
			end = Engine::Vector2::FromJson(*it);
		}
		animation.start = Engine::Vector4(start.x, start.y, 0.0f, 0.0f);
		animation.end = Engine::Vector4(end.x, end.y, 0.0f, 0.0f);
		animation.easingType = Engine::EnumAdapter<EasingType>::FromString(params.value("easingType", "EaseOutSine"))
								   .value_or(EasingType::EaseOutSine);
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

		// 回転も1成分と同じ曲線処理で組み立てる
		return MakeTrailFloatAnimation(params, "start", "end", 0.0f, 0.0f);
	}

	Engine::ParticleTrailPhaseSettings MakeDefaultTrailPhaseSettings(const Engine::ParticleTrailSettings& trail) {

		// Trailの基本値からPhaseの既定設定を作る
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
//	ParticleRenderSettingsBuilder
//============================================================================
Engine::ParticleRenderSettings Engine::MakeParticleRenderSettings(
	PrimitiveRenderSpace space, const ParticleEffectGroup& group) {

	// 共通の形状とMaterialを描画用の値へ移す
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
	settings.renderingLayerMask = group.renderingLayerMask & kRenderingLayerMaskBits;
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
				settings.trailPhaseSettings.back().width =
					MakeTrailFloatAnimation(entry.params, "startScale", "endScale", 0.1f, 0.0f);
			}
			if (entry.id == "TrailColorOverLifetime") {
				settings.trailPhaseSettings.back().color =
					MakeTrailColorAnimation(entry.params, group.trail.startColor, group.trail.endColor);
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
