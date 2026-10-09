#include "GameplayRefactoringTests.h"
#include "InputConfigurationTests.h"
#include "AnimationSnapshotTests.h"
#include "AnimationControllerPlaybackTests.h"
#include "SkeletonAnimationTests.h"
#include "AnimationClipPreviewTests.h"
#include "ParticleEffectRuntimeTests.h"
#include "AudioDecoderTests.h"
#include "RuntimePreloadTests.h"
#include "ShaderCookInputTests.h"
#include "ShaderCookDependencyTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationTrackSampling.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Core/Animation/Controllers/AnimationControllerAsset.h>
#include <Engine/Core/Platform/Input/InputViewMapping.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextLayoutBuilder.h>
#include <Engine/Core/World/Systems/Animation/AnimationEventCollection.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlaybackTime.h>
#include <Engine/Core/World/Systems/Effect/ParticleModuleExecution.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleFloatAnimationSettings.h>
#include <Engine/Core/Foundation/Utility/Random/RandomGenerator.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <array>
#include <cmath>
#include <iostream>
#include <type_traits>

static_assert(std::is_nothrow_move_constructible_v<Engine::AnimationPlayerComponent>);

namespace {

	using namespace Engine;

	bool TestParticleRandomSequence() {

		std::mt19937 first(417u);
		std::mt19937 second(417u);
		std::mt19937 unrelated(31u);
		for (int i = 0; i < 100; ++i) {
			float expected = 0.0f;
			{
				RandomGeneratorScope scope(first);
				expected = RandomGenerator::Generate(-2.0f, 3.0f);
			}
			RandomGeneratorScope scope(second);
			{
				// 別Effectの乱数消費を挟んでも再現性を維持する
				RandomGeneratorScope otherScope(unrelated);
				RandomGenerator::Generate(0, 100);
				RandomGenerator::Generate(0.0f, 1.0f);
			}
			if (RandomGenerator::Generate(-2.0f, 3.0f) != expected) {
				return false;
			}
		}
		ParticleSystemComponent component{};
		component.useAutoRandomSeed = false;
		component.randomSeed = 0xffffffffu;
		nlohmann::json saved = component;
		const auto restored = saved.get<ParticleSystemComponent>();
		return !restored.useAutoRandomSeed && restored.randomSeed == component.randomSeed && first == second;
	}

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
		void OnSpawnBatch(std::span<Particle> particles) override {
			calls_.push_back(tag_ * 10 + static_cast<int>(particles.size()));
		}

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
		ParticleFloatAnimation::ReadAnimationSettings({{"end", 11.0f}}, animation);
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
			{ParticleModuleExecutionMode::PerParticle, {&first, &second}},
			{ParticleModuleExecutionMode::Batch, {&batch}},
			{ParticleModuleExecutionMode::PerParticle, {&last}},
		};
		std::array<Particle, 2> particles{};
		particles[0].id = 1;
		particles[1].id = 2;
		ParticleModuleExecution::ExecuteSpawnModules(particles, phase);
		if (calls != std::vector<int>{11, 21, 12, 22, 32, 41, 42}) {
			return false;
		}

		std::vector<ParticlePhaseDefinition> phases(2);
		phases[0].lifeEndMode = ParticleLifeEndMode::Advance;
		phases[1].lifeEndMode = ParticleLifeEndMode::Clamp;
		Particle particle{};
		particle.age = 5.0f;
		particle.pos = Vector3(2.0f, 3.0f, 4.0f);
		if (!ParticleModuleExecution::AdvancePhaseOnLifeEnd(particle, phases) || particle.phaseIndex != 1 ||
			particle.age != 0.0f || particle.pos != Vector3(2.0f, 3.0f, 4.0f)) {
			return false;
		}
		particle.age = 10.0f;
		return ParticleModuleExecution::AdvancePhaseOnLifeEnd(particle, phases) && particle.age == particle.lifetime;
	}

	bool TestAnimationController() {

		AnimationControllerAsset controller;
		controller.defaultState = "Idle";
		controller.states = {{"Idle", {}}, {"Run", {}}};
		controller.parameters = {
			{"Go", AnimationControllerParameterType::Trigger, false},
			{"Speed", AnimationControllerParameterType::Float, 0.0f},
		};
		controller.transitions = {
			{"Idle", "Run",
				{{"Go", AnimationControllerConditionMode::If}, {"Speed", AnimationControllerConditionMode::Greater, 1.0f}},
				0.2f},
			{"Run", "Idle", {}, 0.1f, true, 1.5f},
		};
		std::string error;
		if (!AnimationControllerEvaluator::Validate(controller, error)) {
			return false;
		}
		AnimationControllerRuntime runtime;
		AnimationControllerEvaluator::Reset(controller, runtime);
		// 不成立の遷移ではTriggerを消費しない
		if (!AnimationControllerEvaluator::SetParameter(controller, runtime, "Go", true) ||
			AnimationControllerEvaluator::SetParameter(controller, runtime, "Speed", true) ||
			AnimationControllerEvaluator::Evaluate(controller, runtime, 0.0f) ||
			!std::get<bool>(*AnimationControllerEvaluator::GetParameter(runtime, "Go"))) {
			return false;
		}
		if (!AnimationControllerEvaluator::SetParameter(controller, runtime, "Speed", 2.0f) ||
			AnimationControllerEvaluator::Evaluate(controller, runtime, 0.0f) != 0 || runtime.state != "Run" ||
			std::get<bool>(*AnimationControllerEvaluator::GetParameter(runtime, "Go"))) {
			return false;
		}
		// Exit Timeは境界に達したframeから成立する
		if (AnimationControllerEvaluator::Evaluate(controller, runtime, 1.4f) ||
			AnimationControllerEvaluator::Evaluate(controller, runtime, 1.5f) != 1 || runtime.state != "Idle") {
			return false;
		}
		const nlohmann::json saved = controller;
		const auto loaded = saved.get<AnimationControllerAsset>();
		if (nlohmann::json(loaded) != saved) {
			return false;
		}
		// Exit Time通過後の条件成立は次の周回まで待つ
		AnimationControllerAsset looping = controller;
		looping.transitions = {{"Idle", "Run", {{"Go", AnimationControllerConditionMode::If}}, 0.1f, true, 0.5f}};
		AnimationControllerEvaluator::Reset(looping, runtime);
		if (AnimationControllerEvaluator::Evaluate(looping, runtime, 0.5f)) {
			return false;
		}
		AnimationControllerEvaluator::SetParameter(looping, runtime, "Go", true);
		if (AnimationControllerEvaluator::Evaluate(looping, runtime, 0.6f) ||
			AnimationControllerEvaluator::Evaluate(looping, runtime, 1.5f) != 0) {
			return false;
		}
		// Any Stateは一覧内の位置によらず状態別の遷移より優先する
		looping.states.push_back({"Other", {}});
		looping.transitions = {{"Idle", "Run", {}}, {"", "Other", {}}};
		AnimationControllerEvaluator::Reset(looping, runtime);
		if (AnimationControllerEvaluator::Evaluate(looping, runtime, 0.0f) != 1 || runtime.state != "Other") {
			return false;
		}
		controller.states.push_back(controller.states.front());
		return !AnimationControllerEvaluator::Validate(controller, error) && !error.empty();
	}

	bool TestAnimationEvaluation() {

		AnimationCurveTrack track;
		track.binding = {"Transform", "localPos", AnimationValueType::Vector3};
		track.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
		track.channels[0].AddKey(0.0f, 1.0f);
		track.channels[0].AddKey(1.0f, 5.0f);
		AnimationPropertyValue value;
		// XとYを別Clipで動かしても互いの成分を消さない
		const AnimationPropertyBinding binding{"Transform", "localPos", AnimationValueType::Vector3};
		std::array<AnimationPreviewBaseValue, 1> base{{{binding, Vector3(10.0f, 20.0f, 30.0f)}}};
		std::array<AnimationContribution, 2> contributions{{
			{{binding, Vector3(50.0f, 20.0f, 30.0f), 1}, 1.0f, 0, false},
			{{binding, Vector3(10.0f, 60.0f, 30.0f), 2}, 1.0f, 0, false},
		}};
		std::vector<AnimationEvaluatedValue> composed;
		AnimationClipEvaluator::ComposeValues(contributions, base, composed);
		if (composed.size() != 1 || composed[0].channelMask != 3 ||
			std::get<Vector3>(composed[0].value) != Vector3(50.0f, 60.0f, 30.0f)) {
			return false;
		}
		// 同優先度は平均し、高優先度とAdditiveを区別する
		contributions[1].evaluated = {binding, Vector3(70.0f, 20.0f, 30.0f), 1};
		AnimationClipEvaluator::ComposeValues(contributions, base, composed);
		if (std::get<Vector3>(composed[0].value).x != 60.0f) {
			return false;
		}
		contributions[1].priority = 1;
		contributions[1].weight = 0.5f;
		AnimationClipEvaluator::ComposeValues(contributions, base, composed);
		if (std::get<Vector3>(composed[0].value).x != 40.0f) {
			return false;
		}
		contributions[1].additive = true;
		AnimationClipEvaluator::ComposeValues(contributions, base, composed);
		if (std::get<Vector3>(composed[0].value).x != 80.0f) {
			return false;
		}
		if (!AnimationTrackSampling::EvaluateTrackWithFallback(track, 0.5f, Vector3(7.0f, 8.0f, 9.0f), value) ||
			std::get<Vector3>(value) != Vector3(3.0f, 8.0f, 9.0f)) {
			return false;
		}
		if (!AnimationValueOperations::CombineValue(
				5.0f, 2.0f, AnimationApplyMode::Multiply, QuaternionMultiplyOrder::BaseThenCurve, value) ||
			std::get<float>(value) != 10.0f) {
			return false;
		}

		AnimationClipAsset clip;
		clip.curveTracks.push_back(track);
		clip.events = {{0.0f, "start"}, {0.5f, "middle"}, {1.0f, "end"}};
		const nlohmann::json saved = clip;
		const AnimationClipAsset restored = saved.get<AnimationClipAsset>();
		if (restored.curveTracks.size() != 1 || restored.events.size() != 3 ||
			restored.curveTracks[0].channels[0].Evaluate(0.5f) != 3.0f) {
			return false;
		}

		AnimationClipRuntime runtime;
		runtime.time = 0.75f;
		runtime.eventStartPending = false;
		runtime.phase = AnimationClipPhase::Play;
		std::vector<AnimationEvent> events;
		std::vector<AnimationPlaybackTime::Interval> intervals;
		AnimationState looping;
		AnimationPlaybackTime::AdvanceLoop(runtime, looping, 1.0f, 0.5f, &intervals);
		AnimationEventCollection::CollectClipEvents(clip, intervals, events);
		if (events.size() != 2 || events[0].name != "end" || events[1].name != "start") {
			return false;
		}

		AnimationState state;
		state.loopCount = 2;
		runtime = {};
		runtime.playing = true;
		runtime.time = 0.75f;
		AnimationPlaybackTime::AdvanceLoop(runtime, state, 1.0f, 1.5f);
		if (runtime.repeatCount != 2 || !runtime.finished || runtime.playing || runtime.time != 1.0f) {
			return false;
		}
		// 16回を超える境界も時間とEventを欠落させない
		runtime = {};
		intervals.clear();
		events.clear();
		AnimationPlaybackTime::AdvanceLoop(runtime, looping, 1.0f, 32.25f, &intervals);
		AnimationEventCollection::CollectClipEvents(clip, intervals, events);
		if (runtime.repeatCount != 32 || runtime.time != 0.25f || events.size() != 97) {
			return false;
		}
		// Eventのない大量周回は同じ時刻へまとめて進める
		runtime = {};
		AnimationPlaybackTime::AdvanceLoop(runtime, looping, 1.0f, 1000000.25f);
		if (runtime.repeatCount != 1000000 || runtime.time != 0.25f || runtime.normalizedTime != 1000000.25) {
			return false;
		}
		// 折返しの端点は一度だけ配送し、戻りは時刻の逆順にする
		runtime = {};
		intervals.clear();
		events.clear();
		AnimationPlaybackTime::AdvancePingPong(runtime, looping, 1.0f, 2.5f, &intervals);
		AnimationEventCollection::CollectClipEvents(clip, intervals, events);
		if (runtime.repeatCount != 1 || runtime.time != 0.5f || events.size() != 6 || events[3].name != "middle" ||
			events[4].name != "start") {
			return false;
		}
		// 逆再生も終端から通過順に配送する
		runtime = {};
		runtime.time = 1.0f;
		intervals.clear();
		events.clear();
		AnimationPlaybackTime::AdvanceLoop(runtime, looping, 1.0f, -0.5f, &intervals);
		AnimationEventCollection::CollectClipEvents(clip, intervals, events);
		return runtime.time == 0.5f && events.size() == 2 && events[0].name == "end" && events[1].name == "middle";
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
		font.glyphMap[U'A'] = {U'A', 1.0f, MSDFPlaneBounds{0.0f, 1.0f, 1.0f, 0.0f}, MSDFAtlasBounds{0, 10, 10, 0}};
		font.glyphMap[U'?'] = {U'?', 0.5f, MSDFPlaneBounds{0.0f, 1.0f, 0.5f, 0.0f}, MSDFAtlasBounds{10, 10, 15, 0}};
		font.glyphMap[U' '] = {U' ', 0.5f, {}, {}};
		if (!TextLayoutBuilder::NeedsTextLayoutRebuild(world, entity, renderer, font) ||
			!TextLayoutBuilder::RebuildTextLayoutCache(font, world, entity, renderer)) {
			return false;
		}
		const auto glyphs = GetTextLayoutGlyphs(world, entity);
		const auto& layout = world.GetComponent<TextLayoutRuntimeComponent>(entity);
		if (glyphs.size() != 3 || glyphs[1].rectMin != Vector2(1.5f, 0.0f) || glyphs[2].rectMin != Vector2(0.0f, 2.0f) ||
			layout.boundsSize != Vector2(2.0f, 3.0f) ||
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
		mapping.SetViewRect(
			view, Vector2(100.0f, 50.0f), Vector2(200.0f, 100.0f), Vector2(400.0f, 200.0f), InputViewCoordinateSpace::Client);
		if (mapping.GetMousePosInView(view, state) != Vector2(100.0f, 100.0f) ||
			mapping.GetMouseMoveValueInView(view, Vector2(3.0f, -2.0f)) != Vector2(6.0f, -4.0f)) {
			return false;
		}
		mapping.SetViewRect(
			view, Vector2(1100.0f, 550.0f), Vector2(200.0f, 100.0f), Vector2(400.0f, 200.0f), InputViewCoordinateSpace::Screen);
		if (mapping.GetMousePosInView(view, state) != Vector2(100.0f, 100.0f)) {
			return false;
		}
		state.mouseScreenPos.x = 1300.0f;
		return !mapping.IsMouseOnView(view, state) && !mapping.GetMousePosInView(view, state).has_value();
	}
}

bool TestGameplayContracts() {

	if (!NEMTests::TestShaderCookInputs() || !NEMTests::TestShaderCookDependencies()) {
		std::cerr << "Shader Cook input contract failed\n";
		return false;
	}
	if (!NEMTests::TestRuntimePreload()) {
		std::cerr << "Runtime preload contract failed\n";
		return false;
	}

	if (!NEMTests::TestAudioWaveReader()) {
		std::cerr << "Audio WAV contract failed\n";
		return false;
	}
	if (!TestParticleRandomSequence() || !NEMTests::TestParticleEffectRuntime()) {
		std::cerr << "Particle random sequence contract failed\n";
		return false;
	}
	if (!TestParticleSettingsPublication()) {
		std::cerr << "Particle settings publication contract failed\n";
		return false;
	}
	if (!TestParticleExecution()) {
		std::cerr << "Particle execution contract failed\n";
		return false;
	}
	if (!TestAnimationController() || !TestAnimationEvaluation() || !NEMTests::TestAnimationSnapshot() ||
		!NEMTests::TestAnimationControllerPlayback() || !NEMTests::TestSkeletonAnimation() ||
		!NEMTests::TestAnimationClipPreview()) {
		std::cerr << "Animation evaluation contract failed\n";
		return false;
	}
	if (!TestTextLayout()) {
		std::cerr << "Text layout contract failed\n";
		return false;
	}
	if (!NEMTests::TestInputConfiguration() || !TestInputViewCoordinates()) {
		std::cerr << "Input view coordinate contract failed\n";
		return false;
	}
	return true;
}
