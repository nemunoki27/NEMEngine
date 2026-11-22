#include "TextCharAnimationSampler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseDocument.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {

	using namespace Engine;

	void AddDiagnostic(std::string& diagnostic, std::string_view source, std::string_view reason) {

		if (!diagnostic.empty()) diagnostic += "\n";
		diagnostic.append(source).append("：").append(reason);
	}

	// 文字中心の移動・角度は0、スケールと乗算色は1を基準にする
	SimpleAnimationValue Neutral(AnimationValueType type, bool unit) {

		const float v = unit ? 1.0f : 0.0f;
		switch (type) {
		case AnimationValueType::Vector2: return Vector2::AnyInit(v);
		case AnimationValueType::Vector3: return Vector3::AnyInit(v);
		case AnimationValueType::Quaternion: return Quaternion::Identity();
		case AnimationValueType::Color3: return Color3(v, v, v);
		case AnimationValueType::Color4: return Color4(v, v, v, v);
		default: return v;
		}
	}

	double SimpleDuration(const SimpleAnimationSettings& settings) {

		const double length = settings.mode == SimpleAnimationMode::Interpolate ? settings.duration :
			(std::max)(settings.period, settings.blendDuration);
		return (settings.delay + length) / settings.speed;
	}

	struct PrefabAnimationCache {
		struct Entry {
			uint64_t contentRevision;
			const char* error;
			std::optional<SimpleAnimationSettings> settings;
		};
		std::weak_ptr<const uint8_t> lifetime;
		uint64_t structureRevision = 0;
		std::unordered_map<AssetID, Entry> entries;
	};

	const PrefabAnimationCache::Entry& LoadPrefabAnimation(ECSWorld& world, AssetDatabase& database, AssetID id) {

		auto& cache = world.GetStorage().Get<PrefabAnimationCache>();
		const auto lifetime = database.GetCacheLifetime();
		if (cache.lifetime.owner_before(lifetime) || lifetime.owner_before(cache.lifetime) ||
			cache.structureRevision != database.GetStructureRevision()) {
			cache.lifetime = lifetime;
			cache.structureRevision = database.GetStructureRevision();
			cache.entries.clear();
		}
		const uint64_t revision = database.GetContentRevision(id);
		auto [it, inserted] = cache.entries.try_emplace(id);
		auto& entry = it->second;
		if (!inserted && entry.contentRevision == revision) return entry;

		entry = {revision, "PrefabのルートにSimpleAnimationが必要です", {}};
		Engine::UUID root;
		const auto entities = PrefabBaseDocument::LoadPrefabBaseEntities(database, id, &root);
		const auto object = entities.find(root);
		if (object == entities.end()) {
			entry.error = "Prefabを読み込めません";
			return entry;
		}
		const auto& components = object->second.components;
		const auto animation = components.find("SimpleAnimation");
		if (animation != components.end()) {
			try {
				entry.settings = animation->get<SimpleAnimationComponent>().settings;
				entry.error = nullptr;
			} catch (const std::exception&) {
				entry.error = "PrefabのSimpleAnimation設定が不正です";
			}
		}
		return entry;
	}

	struct AssetSource {
		const SimpleAnimationSettings* simple = nullptr;
		const AnimationClipAsset* clip = nullptr;
		const char* error = nullptr;
	};

	AssetSource ResolveAsset(ECSWorld& world, AssetDatabase& database, AnimationClipManager* manager, AssetID id) {

		const auto* meta = database.Find(id);
		if (meta && meta->type == AssetType::Prefab) {
			const auto& entry = LoadPrefabAnimation(world, database, id);
			return {entry.settings ? &*entry.settings : nullptr, nullptr, entry.error};
		}
		if (!meta && !GetAnimationClipAssetOverride(world, id)) return {nullptr, nullptr, "参照先のアセットが見つかりません"};
		if (meta && meta->type != AssetType::AnimationClip) return {nullptr, nullptr, "Prefabまたはアニメーションクリップが必要です"};
		const auto* clip = manager ? ResolveAnimationClipAsset(world, *manager, database, id) : nullptr;
		return {nullptr, clip, clip ? nullptr : "アニメーションクリップを読み込めません"};
	}
}

const char* Engine::TextCharAnimationSampler::ValidateSimple(size_t slot, const SimpleAnimationSettings& settings) {

	if (slot >= 4) return "参照欄が不正です";
	const auto type = static_cast<AnimationValueType>(settings.start.index());
	const bool vector = type == AnimationValueType::Vector2 || type == AnimationValueType::Vector3;
	const bool scalar = type == AnimationValueType::Float;
	switch (static_cast<Effect>(slot)) {
	case Effect::Translation: if (!vector) return "移動にはVector2またはVector3が必要です"; break;
	case Effect::Rotation: if (!scalar) return "回転にはfloatが必要です"; break;
	case Effect::Scale: if (!vector && !scalar) return "スケールにはfloat・Vector2・Vector3が必要です"; break;
	case Effect::Color:
		if (!scalar && type != AnimationValueType::Color3 && type != AnimationValueType::Color4)
			return "色にはfloat・Color3・Color4が必要です";
		break;
	}
	if (!IsValidSimpleAnimation(settings) || !std::isfinite(SimpleDuration(settings)) ||
		SimpleDuration(settings) > (std::numeric_limits<float>::max)()) return "再生時間・速度の設定を確認してください";
	return nullptr;
}

std::optional<Engine::TextCharAnimationSampler::ClipTrack> Engine::TextCharAnimationSampler::MatchTrack(
	const AnimationCurveTrack& track) {

	const auto& binding = track.binding;
	const auto& path = binding.propertyPath;
	const auto type = binding.valueType;
	const bool transform = binding.componentName == "Transform";
	Effect effect;
	if (transform && ((path == "localPos" && type == AnimationValueType::Vector3) ||
		(path == "localPos2D" && type == AnimationValueType::Vector2))) effect = Effect::Translation;
	else if (transform && ((path == "localScale" && type == AnimationValueType::Vector3) ||
		(path == "localScale2D" && type == AnimationValueType::Vector2))) effect = Effect::Scale;
	else if (transform && ((path == "localRotationZ" && type == AnimationValueType::Float) ||
		(path == "localRotation" && type == AnimationValueType::Quaternion))) effect = Effect::Rotation;
	else if (binding.componentName == "TextRenderer" && (path == "color" || path == "material.color") &&
		type == AnimationValueType::Color4) effect = Effect::Color;
	else return {};

	uint8_t mask = 0;
	for (size_t c = 0; c < track.channels.size() && c < 4; ++c) {
		if (!track.channels[c].keys.empty()) mask |= static_cast<uint8_t>(1u << c);
	}
	if (!mask || ((effect == Effect::Translation || effect == Effect::Scale) && !(mask & 3))) return {};
	return ClipTrack{&track, effect, mask};
}

uint8_t Engine::TextCharAnimationSampler::ClipChannelMask(const AnimationClipAsset& clip) {

	uint8_t mask = 0;
	for (const auto& track : clip.curveTracks) {
		if (const auto binding = MatchTrack(track)) mask |= static_cast<uint8_t>(1u << static_cast<size_t>(binding->effect));
	}
	return mask;
}

const char* Engine::TextCharAnimationSampler::ValidateAsset(size_t slot, ECSWorld& world, AssetDatabase& database,
	AnimationClipManager& manager, AssetID asset) {

	if (slot >= 4) return "参照欄が不正です";
	const auto source = ResolveAsset(world, database, &manager, asset);
	if (source.error) return source.error;
	if (source.simple) return ValidateSimple(slot, *source.simple);
	return (ClipChannelMask(*source.clip) & (1u << slot)) ? nullptr : "クリップにこの項目のキーがありません";
}

bool Engine::TextCharAnimationSampler::Prepare(ECSWorld& world, Entity owner, SystemContext& context,
	const TextCharAnimationComponent& component, std::string& diagnostic) {

	duration_ = 0;
	channels_ = 0;
	clips_ = {};
	simple_ = {};
	tracks_.clear();
	diagnostic.clear();
	const char* labels[] = { "移動", "Z回転", "スケール", "色" };
	for (size_t i = 0; i < simple_.size(); ++i) {
		const auto effect = static_cast<Effect>(i);
		const SimpleAnimationSettings* source = nullptr;
		if (component.sourceAssets[i]) {
			const auto asset = context.assetDatabase ?
				ResolveAsset(world, *context.assetDatabase, context.animationClipManager, component.sourceAssets[i]) :
				AssetSource{nullptr, nullptr, "アセットデータベースがありません"};
			if (asset.error) {
				AddDiagnostic(diagnostic, labels[i], asset.error);
				continue;
			}
			if (asset.clip) {
				const size_t begin = tracks_.size();
				for (const auto& track : asset.clip->curveTracks) {
					const auto entry = MatchTrack(track);
					if (entry && entry->effect == effect) tracks_.push_back(*entry);
				}
				if (begin == tracks_.size()) {
					AddDiagnostic(diagnostic, labels[i], "クリップにこの項目のキーがありません");
					continue;
				}
				clips_[i] = asset.clip;
				const float length = component.repeat == TextCharAnimationRepeat::LoopCharacters ?
					AnimationClipEvaluator::GetPlaybackDuration(*asset.clip) : asset.clip->duration;
				duration_ = (std::max)(duration_, length);
				channels_ |= static_cast<uint32_t>(TimeChannel::AnimationClip);
				continue;
			}
			source = asset.simple;
		} else {
			const auto& reference = component.SceneSource(i);
			const auto id = component.RuntimeSource(i);
			if (!id && !reference.localFileID) continue;
			const auto entity = id ? world.FindByUUID(id) : reference.Resolve(world, owner);
			const auto* animation = world.TryGetComponent<SimpleAnimationComponent>(entity);
			if (!animation) {
				AddDiagnostic(diagnostic, labels[i], "参照先のSimpleAnimationが見つかりません");
				continue;
			}
			source = &animation->settings;
		}
		if (const char* error = ValidateSimple(i, *source)) {
			AddDiagnostic(diagnostic, labels[i], error);
			continue;
		}
		auto& settings = simple_[i].emplace(*source);
		settings.repeat = SimpleAnimationRepeat::Once;
		settings.cycleCount = 0;
		if (settings.startFromCurrent || settings.mode != SimpleAnimationMode::Interpolate) {
			settings.start = Neutral(static_cast<AnimationValueType>(settings.start.index()), effect == Effect::Scale || effect == Effect::Color);
		}
		duration_ = (std::max)(duration_, static_cast<float>(SimpleDuration(settings)));
		channels_ |= static_cast<uint32_t>(TimeChannel::SimpleAnimation);
	}
	return channels_ != 0;
}

void Engine::TextCharAnimationSampler::Apply(TextCharAnimationPose& pose, Effect effect,
	const SimpleAnimationValue& value, uint8_t mask) {

	if (effect == Effect::Color) {
		if (const auto* alpha = std::get_if<float>(&value)) pose.color.a = *alpha;
		else if (const auto* rgb = std::get_if<Color3>(&value)) {
			pose.color.r = rgb->r; pose.color.g = rgb->g; pose.color.b = rgb->b;
		} else if (const auto* rgba = std::get_if<Color4>(&value)) {
			if (mask & 1) pose.color.r = rgba->r;
			if (mask & 2) pose.color.g = rgba->g;
			if (mask & 4) pose.color.b = rgba->b;
			if (mask & 8) pose.color.a = rgba->a;
		}
		return;
	}
	if (effect == Effect::Rotation) {
		if (const auto* z = std::get_if<float>(&value)) pose.transform.rotation = *z;
		else pose.transform.rotation = Quaternion::ToEulerDegrees(std::get<Quaternion>(value)).z;
		return;
	}
	Vector2 xy{};
	if (const auto* xyz = std::get_if<Vector3>(&value)) xy = { xyz->x, xyz->y };
	else if (const auto* vector = std::get_if<Vector2>(&value)) xy = *vector;
	else xy = Vector2::AnyInit(std::get<float>(value));
	auto& destination = effect == Effect::Scale ? pose.transform.scale : pose.transform.translation;
	if (mask & 1) destination.x = xy.x;
	if (mask & 2) destination.y = xy.y;
}

Engine::TextCharAnimationPose Engine::TextCharAnimationSampler::Sample(float localTime, bool loopCharacters) const {

	TextCharAnimationPose pose;
	const float time = (std::max)(0.0f, localTime);
	for (const auto& entry : tracks_) {
		const auto& clip = *clips_[static_cast<size_t>(entry.effect)];
		AnimationResolvedTime resolved{};
		resolved.clipTime = (std::min)(time, clip.duration);
		if (loopCharacters) {
			if (clip.loop) resolved = AnimationClipEvaluator::ResolveClipEvaluationTime(clip, time);
			else if (clip.duration > 0.0f) resolved.clipTime = std::fmod(time, clip.duration);
		}
		const auto& track = *entry.track;
		AnimationPropertyValue value;
		if (!AnimationClipEvaluator::EvaluateTrack(track, resolved, clip, value)) continue;
		if (track.applyMode != AnimationApplyMode::Override) {
			const auto base = Neutral(track.binding.valueType, entry.effect == Effect::Scale || entry.effect == Effect::Color);
			AnimationPropertyValue combined;
			if (!AnimationValueOperations::CombineValue(base, value, track.applyMode, track.quaternionMultiplyOrder, combined)) continue;
			value = combined;
		}
		Apply(pose, entry.effect, value, entry.mask);
	}

	for (size_t i = 0; i < simple_.size(); ++i) {
		if (!simple_[i]) continue;
		const auto& settings = *simple_[i];
		const bool wave = settings.mode != SimpleAnimationMode::Interpolate;
		const double length = wave ? settings.period : settings.duration;
		const double elapsed = static_cast<double>(time) * settings.speed;
		double sampleTime = elapsed;
		if (loopCharacters && elapsed >= settings.delay && length > 0.0) {
			sampleTime = settings.delay + std::fmod(elapsed - settings.delay, length);
		}
		auto sample = EvaluateSimpleAnimation(settings, sampleTime);
		if (!sample.valid) continue;
		if (wave && settings.blendDuration > 0.0) {
			const float blend = static_cast<float>(std::clamp((elapsed - settings.delay) / settings.blendDuration, 0.0, 1.0));
			sample.value = BlendSimpleAnimation(settings.start, sample.value, EasedValue(settings.easing, blend));
		}
		Apply(pose, static_cast<Effect>(i), sample.value, 0x0f);
	}
	return pose;
}
