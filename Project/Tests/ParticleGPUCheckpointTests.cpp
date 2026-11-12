#include "ParticleGPUCheckpointTests.h"
#include "ParticleGPUFixture.h"
#include "ParticleGPUFeedbackFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/World/Systems/Effect/ParticleSimulationReplay.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Timeline/TimelineClipInstance.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUReplayIdentity.h>

// c++
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <array>

using namespace Engine;
using namespace NEMTests;

namespace {

	template<typename Callback>
	void ForEachBuffer(ParticleGPUInstance& instance, const Callback& callback) {

		callback(instance.particles);
		callback(instance.moduleStates);
		callback(instance.statistics);
		callback(instance.trails);
		callback(instance.trailPoints);
		callback(instance.trailFreeList);
		callback(instance.ribbonOrder);
	}

	void Fill(ParticleGPUFixture& fixture, ParticleGPUInstance& instance, uint8_t pattern) {

		ForEachBuffer(instance, [&](auto& buffer) {
			const auto bytes = static_cast<size_t>(buffer.GetResource()->GetDesc().Width);
			ComPtr<ID3D12Resource> upload;
			DxUtils::CreateUploadBufferResource(fixture.device.Get(), upload, bytes);
			void* memory = nullptr;
			if (FAILED(upload->Map(0, nullptr, &memory))) throw std::runtime_error("粒子途中状態の転送に失敗しました");
			std::memset(memory, pattern, bytes);
			upload->Unmap(0, nullptr);
			buffer.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_DEST);
			fixture.commands.GetCommandList()->CopyBufferRegion(buffer.GetResource(), 0, upload.Get(), 0, bytes);
			fixture.retirement.Retire(std::move(upload));
		});
	}

	bool CheckFeedbackContinuity(ParticleGPUFixture& fixture) {

		auto prepare = fixture.MakePipeline("particleGPUEventPrepare.CS.hlsl");
		if (!prepare) return false;
		ParticleGPUFeedbackFixture input(fixture);
		input.values.source = input.identities[1];
		input.previousData[0].source = input.identities[0];
		input.previousData[0].target = input.identities[1];
		input.previousCount = 1;
		ParticleGPUState state;
		state.deterministicReplay = true;
		state.epoch = 5;
		for (uint32_t mode = 0; mode < 6; ++mode) {
			state.pending.assign(1, ParticleGPUSimulationStep{});
			state.suppressFeedback = mode == 3;
			state.deterministicReplay = mode != 4;
			if (mode == 0) state.pending.front().restoreCheckpoint = std::make_shared<ParticleGPUCheckpoint>();
			if (mode == 1) state.pending.clear();
			if (mode == 2 || mode == 5) state.pending.front().randomSequence = 0;
			if (mode == 5) state.pending.front().suppressFeedback = true;
			const auto epoch = ResolveParticleGPUFeedbackEpoch(state, 1);
			const bool preserved = mode == 0 || mode == 1 || mode == 5;
			if (epoch != (mode == 3 ? 0 : preserved ? 1 : 5)) return false;
			input.identities = {{Entity{1, 0}, 11, epoch}, {Entity{2, 0}, 12, 1}};
			if (!epoch) input.identities.erase(input.identities.begin());
			input.values.systemCount = static_cast<uint32_t>(input.identities.size());
			input.Dispatch(*prepare, 1);
			input.Dispatch(*prepare, 2);
			const auto bytes = fixture.Read(input.childCounts, sizeof(uint32_t));
			uint32_t count = 0;
			std::memcpy(&count, bytes.data(), sizeof(count));
			if (count != (preserved ? 1u : 0u)) return false;
		}
		return fixture.CheckMessages();
	}
}

bool NEMTests::CheckParticleSimulationReplay() {

	ParticleInstanceState independent;
	independent.emitter.particles.Configure(1);
	std::array<Vector3, 1> speeds{Vector3(1, 2, 3)};
	Particle particle;
	particle.rotationSpeeds = speeds;
	particle.trailIndex = particle.trailGeneration = UINT32_MAX;
	independent.emitter.particles.Append(particle);
	ParticleSimulationCheckpoint independentCheckpoint(independent);
	independent.emitter.particles.GetMutable(0).rotationSpeeds.front().x = 9;
	if (!independentCheckpoint.Restore(independent)) return false;
	const auto restoredParticle = independent.emitter.particles.GetMutable(0);
	if (restoredParticle.rotationSpeeds.front().x != 1 || restoredParticle.trailGeneration != UINT32_MAX) return false;
	ECSWorld world;
	auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
	settings->phases.front().modules.clear();
	settings->phases.front().lifetime = ParticleValue<float>{0.7f};
	settings->emitter.shape = ParticleEmitterShape::Point;
	settings->emitter.maxParticles = 128;
	settings->emitter.emitCount = ParticleValue<uint32_t>{1u};
	settings->emitter.emitInterval = 0.05f;
	settings->emitter.speed.type = ParticleValueType::Random;
	settings->emitter.speed.min = 1;
	settings->emitter.speed.max = 3;
	settings->trail.enabled = true;
	settings->trail.maxTrails = 64;
	settings->trail.maxPoints = 8;
	settings->trail.keepAfterParticleDeath = true;
	settings->trail.pointLifetime = 0.3f;
	ParticlePhaseParentSettings parent;
	ParticleInstanceState instance;
	ParticleSimulationReplay replay;
	replay.Reset(812u, false);
	const auto sample = [](double time, Matrix4x4& transform) {
		transform = Matrix4x4::MakeTranslateMatrix(Vector3(static_cast<float>(time * time), 0, static_cast<float>(time)));
		return true;
	};
	const auto evaluate = [&](double time, uint64_t revision = 1) {
		replay.Evaluate(world, instance, settings, ParticleSimulationTarget::CPU, parent, false, time, revision, true, sample);
	};
	const auto signature = [&] {
		std::vector<float> values;
		const auto& emitter = instance.emitter;
		for (const auto particle : emitter.particles) {
			values.insert(values.end(), {static_cast<float>(particle.id), particle.age, particle.lifetime,
				particle.worldPos.x, particle.worldPos.y, particle.worldPos.z, particle.velocity.y});
		}
		for (const auto handle : emitter.trails.GetHandles()) {
			const auto& trail = *emitter.trails.TryGet(handle);
			values.insert(values.end(), {static_cast<float>(trail.count), trail.head.position.x, trail.head.position.y});
			for (uint32_t i = 0; i < trail.count; ++i) {
				const auto& point = emitter.trails.GetPoint(trail, i);
				values.insert(values.end(), {point.position.x, point.position.y, point.position.z, point.age});
			}
		}
		return values;
	};
	evaluate(2.137);
	if (instance.emitter.particles.empty() || instance.emitter.trails.empty()) return false;
	const auto expected = signature();
	const auto equal = [&](const std::vector<float>& values) {
		if (values.size() != expected.size()) return false;
		for (size_t i = 0; i < values.size(); ++i) if (std::abs(values[i] - expected[i]) > 0.00001f) return false;
		return true;
	};
	// 端数時刻から逆再生しても固定境界の粒子と尾を再現する
	evaluate(0.093);
	evaluate(0.426);
	evaluate(1.024);
	evaluate(2.137);
	if (!equal(signature())) return false;
	// 一度戻った直後の同時刻評価で尾や乱数を進めない
	evaluate(2.137);
	if (!equal(signature())) return false;
	evaluate(20.137);
	if (replay.GetCheckpointCount() > ParticleSimulationReplay::kMaxCheckpoints ||
		replay.GetCheckpointMemoryBytes() > ParticleSimulationReplay::kMemoryBudget) return false;
	evaluate(2.137);
	if (!equal(signature())) return false;
	// 編集世代が変わったら過去の途中状態を再利用しない
	evaluate(2.137, 2);
	if (!equal(signature())) return false;
	// GPU実体を複製せず要求だけを積み、未完了の状態は復元に使わない
	ParticleInstanceState gpu;
	ParticleSimulationReplay gpuReplay;
	gpuReplay.Reset(812u, false);
	gpuReplay.Evaluate(world, gpu, settings, ParticleSimulationTarget::GPU, parent, false, 0.137, 1, true, sample);
	const auto owner = gpu.emitter.gpu;
	if (!owner || owner->pending.empty()) return false;
	if (IsParticleGPUReplayReady(*owner, owner->epoch)) return false;
	const auto firstEpoch = owner->epoch;
	ParticleSimulationCheckpoint gpuCheckpoint(gpu);
	owner->pending.back().captureCheckpoint->available = true;
	ParticleInstanceState otherOwner;
	otherOwner.emitter.gpu = std::make_shared<ParticleGPUState>();
	if (gpuCheckpoint.Restore(otherOwner)) return false;
	owner->pending.back().captureCheckpoint->available = false;
	gpuReplay.Evaluate(world, gpu, settings, ParticleSimulationTarget::GPU, parent, false, 0.051, 1, true, sample);
	if (gpu.emitter.gpu != owner || owner->epoch <= firstEpoch || owner->pending.empty() ||
		!owner->pending.back().suppressFeedback || owner->pending.back().randomSequence != 4) return false;
	// 速度ゼロのsourceはClip開始時の姿勢で固定して準備する
	const Entity actor = world.CreateEntity();
	world.AddComponent<TransformComponent>(actor);
	world.AddComponent<ParticleSystemComponent>(actor);
	SetParticleSystemSettings(world, actor, *settings);
	TimelineAsset timeline;
	TimelineTrack track;
	track.kind = TimelineTrackKind::Particle;
	TimelineClip clip;
	clip.data = CreateDefaultTimelineClipData(track.kind);
	clip.start = 2;
	clip.speed = 0;
	clip.clipIn = 0.05;
	SystemContext systems;
	TimelineControlOwner control{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
	TimelineEvaluationContext context{world, systems, actor, actor, timeline, track, clip, {}, control};
	context.mode = TimelineEvaluationMode::Preview;
	context.localTime = clip.clipIn;
	bool frozen = true;
	context.sampleTargetTransform = [&](double time, Entity target, Matrix4x4& transform) {
		frozen &= target == actor && std::isfinite(time) && time == clip.start;
		return sample(time, transform);
	};
	std::string diagnostic;
	auto instanceClip = CreateBuiltinTimelineClip(track, clip, diagnostic);
	if (!instanceClip || !instanceClip->Enter(context, diagnostic)) return false;
	instanceClip->Evaluate(context);
	instanceClip->Exit(context, TimelineExitReason::Stopped);
	ReleaseTimelineControls(world, control);
	return frozen;
}

bool NEMTests::CheckParticleGPUCheckpoints(ParticleGPUFixture& fixture) {

	auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
	settings->emitter.maxParticles = 257;
	settings->trail.enabled = true;
	settings->trail.maxTrails = 5;
	settings->trail.maxPoints = 8;
	ParticleDefinitionCompiler compiler;
	auto state = std::make_shared<ParticleGPUState>();
	state->definition = compiler.Resolve(settings);
	state->program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*state->definition));
	ParticleGPUInstance instance;
	instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
	Fill(fixture, instance, 0x21);
	auto checkpoint = std::make_shared<ParticleGPUCheckpoint>();
	CaptureParticleGPUCheckpoint(fixture.device.Get(), fixture.commands, fixture.retirement, instance,
		checkpoint, fixture.queue.GetCompletedFenceValue());
	if (!checkpoint->available || !checkpoint->memoryBytes) return false;
	const auto bytes = checkpoint->memoryBytes;
	auto budget = instance.checkpointBudget;
	if (!budget || budget->allocated != bytes) return false;
	// 同じ提出内で途中状態を壊してから復元する
	Fill(fixture, instance, 0x72);
	if (!RestoreParticleGPUCheckpoint(fixture.commands, instance, checkpoint)) return false;
	bool exact = true;
	ForEachBuffer(instance, [&](auto& buffer) {
		buffer.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto data = fixture.ReadResource(buffer.GetResource(), static_cast<size_t>(buffer.GetResource()->GetDesc().Width));
		for (const auto value : data) if (value != 0x21) exact = false;
	});
	if (!exact) return false;
	// 他Emitterと共有する予算が尽きた場合は現在の描画を維持する
	const auto allocated = budget->allocated;
	budget->allocated = ParticleGPUCheckpointBudget::kMemoryBudget;
	auto unavailable = std::make_shared<ParticleGPUCheckpoint>();
	CaptureParticleGPUCheckpoint(fixture.device.Get(), fixture.commands, fixture.retirement, instance,
		unavailable, fixture.queue.GetCompletedFenceValue());
	budget->allocated = allocated;
	if (unavailable->available || RestoreParticleGPUCheckpoint(fixture.commands, instance, unavailable)) return false;
	checkpoint.reset();
	instance.checkpoints.clear();
	budget->Collect(fixture.queue.GetCompletedFenceValue());
	if (budget->allocated) return false;
	// 破棄しても提出が完了するまでは予算を再利用しない
	auto pending = std::make_shared<ParticleGPUCheckpoint>();
	CaptureParticleGPUCheckpoint(fixture.device.Get(), fixture.commands, fixture.retirement, instance,
		pending, fixture.queue.GetCompletedFenceValue());
	if (!pending->available) return false;
	pending.reset();
	instance.checkpoints.clear();
	budget->Collect(fixture.queue.GetCompletedFenceValue());
	if (budget->allocated != bytes) return false;
	fixture.Finish();
	budget->Collect(fixture.queue.GetCompletedFenceValue());
	return budget->allocated == 0 && CheckFeedbackContinuity(fixture) && fixture.CheckMessages();
}
