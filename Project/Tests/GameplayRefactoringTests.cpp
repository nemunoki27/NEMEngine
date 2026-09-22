#include "GameplayRefactoringTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationTrackSampling.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Platform/Input/InputViewMapping.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextLayoutBuilder.h>
#include <Engine/Core/World/Systems/Animation/AnimationEventCollection.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlaybackTime.h>
#include <Engine/Core/World/Systems/Effect/ParticleModuleExecution.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleFloatAnimationSettings.h>

// c++
#include <array>
#include <cmath>
#include <iostream>

namespace {

	using namespace Engine;

	//============================================================================
	//	RecordingParticleModule class
	//	Module呼出しと対象粒子の順序を記録する
	//============================================================================
	class RecordingParticleModule : public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RecordingParticleModule(std::vector<int>& calls, int tag) : calls_(calls), tag_(tag) {}

		void FromJson([[maybe_unused]] const nlohmann::json& params) override {}
		nlohmann::json ToJson() const override { return nlohmann::json::object(); }
		void OnSpawn(Particle& particle) override { calls_.push_back(tag_ * 10 + static_cast<int>(particle.id)); }
		void OnSpawnBatch(std::span<Particle> particles) override { calls_.push_back(tag_ * 10 + static_cast<int>(particles.size())); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<int>& calls_;
		int tag_;
	};

	bool TestParticleSettingsPublication() {

		ParticleTrailSizeOverLifetimeModule trailSize;
		if (trailSize.GetSettings().startScale != 0.1f ||
			trailSize.GetUpdateExecutionMode() != ParticleModuleExecutionMode::None) {
			return false;
		}
		ParticleShapeOverLifetimeModule module;
		module.FromJson(nlohmann::json::object());
		auto settings = module.GetSettings();
		auto& radius = settings.parameters.at(ParticleShapeAnimation::kRingOuterRadius);
		radius.mode = ParticleMaterialParameterMode::Constant;
		radius.constant.x = 9.0f;
		module.SetSettings(settings);
		// 呼出側の設定破棄後も確定した値と評価用参照を維持する
		settings.parameters.clear();
		Particle particle{};
		module.OnSpawn(particle);
		if (particle.shapeData.params0.x != 9.0f) {
			return false;
		}
		ParticleShapeOverLifetimeModule restored;
		restored.FromJson(module.ToJson());
		restored.OnSpawn(particle);
		if (particle.shapeData.params0.x != 9.0f || restored.ToJson() != module.ToJson()) {
			return false;
		}

		ParticleFloatAnimationSettings animation;
		animation.start = 3.0f;
		animation.end = 7.0f;
		animation.easingType = EasingType::Linear;
		ParticleFloatAnimation::ReadAnimationSettings({ { "end", 11.0f } }, animation);
		if (animation.start != 3.0f || ParticleFloatAnimation::EvaluateAnimation(animation, 0.5f) != 7.0f) {
			return false;
		}
		const auto saved = ParticleFloatAnimation::WriteAnimationSettings(animation);
		ParticleFloatAnimation::ReadAnimationSettings(nlohmann::json::array(), animation);
		if (saved != ParticleFloatAnimation::WriteAnimationSettings(animation)) {
			return false;
		}
		ParticleFloatAnimationSettings roundTrip;
		ParticleFloatAnimation::ReadAnimationSettings(saved, roundTrip);
		return saved == ParticleFloatAnimation::WriteAnimationSettings(roundTrip);
	}

	bool TestParticleExecution() {

		std::vector<int> calls;
		RecordingParticleModule first(calls, 1), second(calls, 2), batch(calls, 3), last(calls, 4);
		ParticlePhaseDefinition phase;
		phase.spawnExecution = {
			{ ParticleModuleExecutionMode::PerParticle, { &first, &second } },
			{ ParticleModuleExecutionMode::Batch, { &batch } },
			{ ParticleModuleExecutionMode::PerParticle, { &last } },
		};
		std::array<Particle, 2> particles{};
		particles[0].id = 1;
		particles[1].id = 2;
		ParticleModuleExecution::ExecuteSpawnModules(particles, phase);
		if (calls != std::vector<int>{ 11, 21, 12, 22, 32, 41, 42 }) {
			return false;
		}

		std::vector<ParticlePhaseDefinition> phases(2);
		phases[0].lifeEndMode = ParticleLifeEndMode::Advance;
		phases[1].lifeEndMode = ParticleLifeEndMode::Clamp;
		Particle particle{};
		particle.age = 5.0f;
		particle.pos = Vector3(2.0f, 3.0f, 4.0f);
		if (!ParticleModuleExecution::AdvancePhaseOnLifeEnd(particle, phases) ||
			particle.phaseIndex != 1 || particle.age != 0.0f || particle.pos != Vector3(2.0f, 3.0f, 4.0f)) {
			return false;
		}
		particle.age = 10.0f;
		return ParticleModuleExecution::AdvancePhaseOnLifeEnd(particle, phases) && particle.age == particle.lifetime;
	}

	bool TestAnimationEvaluation() {

		AnimationCurveTrack track;
		track.binding = { "Transform", "localPos", AnimationValueType::Vector3 };
		track.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
		track.channels[0].AddKey(0.0f, 1.0f);
		track.channels[0].AddKey(1.0f, 5.0f);
		AnimationPropertyValue value;
		if (!AnimationTrackSampling::EvaluateTrackWithFallback(track, 0.5f, Vector3(7.0f, 8.0f, 9.0f), value) ||
			std::get<Vector3>(value) != Vector3(3.0f, 8.0f, 9.0f)) {
			return false;
		}
		if (!AnimationValueOperations::CombineValue(5.0f, 2.0f, AnimationApplyMode::Multiply,
			QuaternionMultiplyOrder::BaseThenCurve, value) || std::get<float>(value) != 10.0f) {
			return false;
		}

		AnimationClipAsset clip;
		clip.curveTracks.push_back(track);
		clip.events = { { 0.0f, "start" }, { 0.5f, "middle" }, { 1.0f, "end" } };
		const nlohmann::json saved = clip;
		const AnimationClipAsset restored = saved.get<AnimationClipAsset>();
		if (restored.curveTracks.size() != 1 || restored.events.size() != 3 ||
			restored.curveTracks[0].channels[0].Evaluate(0.5f) != 3.0f) {
				return false;
			}

		AnimationClipRuntime runtime;
		runtime.time = 0.25f;
		runtime.repeatCount = 1;
		runtime.phase = AnimationClipPhase::Play;
		std::vector<AnimationEvent> events;
		AnimationEventCollection::CollectClipEvents(clip, AnimationWrapMode::Loop, 0.75f, 1,
			AnimationClipPhase::Play, 0, runtime, 1.0f, events);
		if (events.size() != 2 || events[0].name != "end" || events[1].name != "start") {
			return false;
		}

		AnimationState state;
		state.loopCount = 2;
		runtime = {};
		runtime.playing = true;
		runtime.time = 0.75f;
		AnimationPlaybackTime::AdvanceLoop(runtime, state, 1.0f, 1.5f);
		return runtime.repeatCount == 2 && runtime.finished && !runtime.playing && runtime.time == 1.0f;
	}

	bool TestTextLayout() {

		ECSWorld world(ECSWorldKind::Authoring);
		const Entity entity = world.CreateEntity();
		world.AddComponent<TextRendererComponent>(entity);
		auto& renderer = world.GetComponent<TextRendererComponent>(entity);
		renderer.text = "A X\nA";
		renderer.fontSize = 1.0f;
		renderer.charSpacing = 0.0f;
		MSDFFontAsset font;
		font.contentRevision = 1;
		font.atlasWidth = 100;
		font.atlasHeight = 100;
		font.metrics.elementSize = 1.0f;
		font.metrics.lineHeight = 2.0f;
		font.glyphMap[U'A'] = { U'A', 1.0f, MSDFPlaneBounds{ 0.0f, 1.0f, 1.0f, 0.0f }, MSDFAtlasBounds{ 0, 10, 10, 0 } };
		font.glyphMap[U'?'] = { U'?', 0.5f, MSDFPlaneBounds{ 0.0f, 1.0f, 0.5f, 0.0f }, MSDFAtlasBounds{ 10, 10, 15, 0 } };
		font.glyphMap[U' '] = { U' ', 0.5f, {}, {} };
		if (!TextLayoutBuilder::NeedsTextLayoutRebuild(world, entity, renderer, font) ||
			!TextLayoutBuilder::RebuildTextLayoutCache(font, world, entity, renderer)) {
			return false;
		}
		const auto glyphs = GetTextLayoutGlyphs(world, entity);
		const auto& layout = world.GetComponent<TextLayoutRuntimeComponent>(entity);
		if (glyphs.size() != 3 || glyphs[1].rectMin != Vector2(1.5f, 0.0f) ||
			glyphs[2].rectMin != Vector2(0.0f, 2.0f) || layout.boundsSize != Vector2(2.0f, 3.0f) ||
			TextLayoutBuilder::NeedsTextLayoutRebuild(world, entity, renderer, font)) {
				return false;
			}
		++font.contentRevision;
		return TextLayoutBuilder::NeedsTextLayoutRebuild(world, entity, renderer, font);
	}

	bool TestInputViewCoordinates() {

		InputViewMapping mapping;
		InputDeviceState state{};
		state.mousePos = Vector2(150.0f, 100.0f);
		state.mouseScreenPos = Vector2(1150.0f, 600.0f);
		const InputViewArea view = InputViewArea::Game;
		mapping.SetViewRect(view, Vector2(100.0f, 50.0f), Vector2(200.0f, 100.0f),
			Vector2(400.0f, 200.0f), InputViewCoordinateSpace::Client);
		if (mapping.GetMousePosInView(view, state) != Vector2(100.0f, 100.0f) ||
			mapping.GetMouseMoveValueInView(view, Vector2(3.0f, -2.0f)) != Vector2(6.0f, -4.0f)) {
				return false;
			}
		mapping.SetViewRect(view, Vector2(1100.0f, 550.0f), Vector2(200.0f, 100.0f),
			Vector2(400.0f, 200.0f), InputViewCoordinateSpace::Screen);
		if (mapping.GetMousePosInView(view, state) != Vector2(100.0f, 100.0f)) {
			return false;
		}
		state.mouseScreenPos.x = 1300.0f;
		return !mapping.IsMouseOnView(view, state) && !mapping.GetMousePosInView(view, state).has_value();
	}
}

bool TestGameplayContracts() {

	if (!TestParticleSettingsPublication()) {
		std::cerr << "Particle settings publication contract failed\n";
		return false;
	}
	if (!TestParticleExecution()) {
		std::cerr << "Particle execution contract failed\n";
		return false;
	}
	if (!TestAnimationEvaluation()) {
		std::cerr << "Animation evaluation contract failed\n";
		return false;
	}
	if (!TestTextLayout()) {
		std::cerr << "Text layout contract failed\n";
		return false;
	}
	if (!TestInputViewCoordinates()) {
		std::cerr << "Input view coordinate contract failed\n";
		return false;
	}
	return true;
}
