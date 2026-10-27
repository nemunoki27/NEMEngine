#include "ParticleSystemRuntimeTests.h"
#include "ParticleGPUFixture.h"
#include "GPUParticleShapeTests.h"
#include "ParticleGPUEmissiveTests.h"
#include "ParticleGPUModuleParityTests.h"
#include "GPUInclusiveScanTests.h"
#include "ParticleSourceTests.h"
#include "ParticleWorldTests.h"
#include "ParticleDepthSnapshotTests.h"
#include "ParticleFeedbackTests.h"
#include "ParticleGPUSourceSpawnTests.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <chrono>
#include <cstring>
#include <iostream>
#include <unordered_set>

using namespace Engine;
using namespace NEMTests;

namespace {

	ParticleGPUSimulationStep MakeStep(uint32_t capacity) {

		auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
		settings->emitter.maxParticles = capacity;
		settings->emitter.shape = ParticleEmitterShape::Point;
		settings->emitter.speed = ParticleValue<float>{2.0f};
		settings->phases[0].lifetime = ParticleValue<float>{0.5f};
		settings->phases[0].lifeEndMode = ParticleLifeEndMode::Advance;
		settings->phases.emplace_back();
		settings->phases[1].lifetime = ParticleValue<float>{0.5f};
		ParticleDefinitionCompiler definitions;
		ParticleGPUSimulationStep step;
		step.definition = definitions.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		step.parents.resize(2);
		step.spawnCount = capacity;
		step.seed = 412;
		step.deltaTime = 0.01f;
		return step;
	}

	uint32_t ReadCount(ParticleGPUFixture& fixture, ParticleGPUInstance& instance) {

		const auto bytes = fixture.Read(instance.statistics, sizeof(ParticleStatisticsGPU));
		ParticleStatisticsGPU statistics;
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		return statistics.particleCount;
	}

	bool CheckSimulation(ParticleGPUFixture& fixture, const PipelineState& pipeline, uint32_t capacity) {

		auto step = MakeStep(capacity);
		if (!step.program->unsupportedReason.empty()) return false;
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		const auto start = std::chrono::steady_clock::now();
		fixture.Simulate(instance, pipeline, step, 0);
		fixture.Simulate(instance, pipeline, step, 5);
		fixture.Simulate(instance, pipeline, step, 2);
		if (ReadCount(fixture, instance) != capacity) return false;
		std::cout << "GPU particle capacity=" << capacity << " initialization+readback ms=" <<
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << '\n';

		// 同じ属性領域でPhaseを遷移し、親更新だけのframeでは寿命を進めない
		step.spawnCount = 0;
		step.deltaTime = 0.6f;
		fixture.Upload(instance, step);
		fixture.Simulate(instance, pipeline, step, 1);
		fixture.Simulate(instance, pipeline, step, 3);
		fixture.Simulate(instance, pipeline, step, 2);
		if (ReadCount(fixture, instance) != capacity) return false;
		instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		auto bytes = fixture.ReadResource(instance.particles.GetResource(), sizeof(ParticleAttributesGPU));
		ParticleAttributesGPU sample;
		std::memcpy(&sample, bytes.data(), sizeof(sample));
		if (sample.phase != 1 || sample.positionAge.w != 0) return false;
		fixture.Upload(instance, step);
		fixture.Simulate(instance, pipeline, step, 4);
		fixture.Simulate(instance, pipeline, step, 3);
		fixture.Simulate(instance, pipeline, step, 2);
		if (ReadCount(fixture, instance) != capacity) return false;
		fixture.Upload(instance, step);
		fixture.Simulate(instance, pipeline, step, 1);
		fixture.Simulate(instance, pipeline, step, 3);
		fixture.Simulate(instance, pipeline, step, 2);
		if (ReadCount(fixture, instance) != 0) return false;

		// Clear後の再利用とダブルバッファリングの切替を確認する
		for (uint32_t count : {1u, 2u}) {
			fixture.ChangeFrameCount(count);
			step.spawnCount = capacity / 2;
			fixture.Upload(instance, step);
			fixture.Simulate(instance, pipeline, step, 0);
			fixture.Simulate(instance, pipeline, step, 5);
			fixture.Simulate(instance, pipeline, step, 2);
			if (ReadCount(fixture, instance) != capacity / 2) return false;
		}
		return fixture.CheckMessages();
	}

	void DispatchTrail(ParticleGPUFixture& fixture, ParticleGPUInstance& instance, const PipelineState& pipeline,
		const ParticleTrailSettings& settings, uint32_t operation, float deltaTime) {

		instance.trails.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		instance.trailPoints.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		instance.trailFreeList.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		instance.ribbonOrder.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		auto values = MakeParticleGPUTrailConstants(settings, instance.capacity);
		values.operation = operation;
		values.deltaTime = deltaTime;
		const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), values);
		auto* commands = fixture.commands.GetCommandList();
		commands->SetComputeRootSignature(pipeline.GetRootSignature());
		commands->SetPipelineState(pipeline.GetComputePipeline());
		RootBindingCommand::SetComputeCBV(commands, pipeline.FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
		RootBindingCommand::SetComputeSRV(commands, pipeline.FindBindingByName("gGPUParents", ShaderBindingKind::SRV), instance.parents.GetGPUAddress());
		const auto uav = [&](const char* name, auto& buffer) {
			RootBindingCommand::SetComputeUAV(commands, pipeline.FindBindingByName(name, ShaderBindingKind::UAV), buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
		};
		uav("gGPUParticles", instance.particles);
		uav("gGPUStatistics", instance.statistics);
		uav("gGPUTrails", instance.trails);
		uav("gGPUTrailPoints", instance.trailPoints);
		uav("gGPUTrailFreeList", instance.trailFreeList);
		uav("gGPURibbonOrder", instance.ribbonOrder);
		commands->Dispatch(((operation == 2 ? instance.capacity : instance.trailCapacity) + 255) / 256, 1, 1);
		fixture.commands.UAVBarrierAll();
	}

	bool CheckTrailCompaction(ParticleGPUFixture& fixture, ParticleGPUInstance& instance,
		const ParticleTrailSettings& settings, const PipelineState& pipeline) {

		StructuredRWBuffer<ParticleGPUViewResources::Key> segments;
		StructuredRWBuffer<ParticleUInt4GPU> arguments;
		segments.Init(fixture.device.Get(), &fixture.descriptors);
		arguments.Init(fixture.device.Get(), &fixture.descriptors);
		segments.EnsureCapacity(instance.trailCapacity * instance.pointCapacity);
		arguments.EnsureCapacity(1);
		segments.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		arguments.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		instance.trails.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		instance.trailPoints.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		instance.ribbonOrder.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		const ParticleGPUViewConstants view{};
		const auto viewAllocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), view);
		auto trail = MakeParticleGPUTrailConstants(settings, instance.capacity);
		for (uint32_t operation : {0u, 1u}) {
			trail.operation = operation;
			const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), trail);
			auto* commands = fixture.commands.GetCommandList();
			commands->SetComputeRootSignature(pipeline.GetRootSignature());
			commands->SetPipelineState(pipeline.GetComputePipeline());
			RootBindingCommand::SetComputeCBV(commands, pipeline.FindBindingByName("ParticleGPUViewConstants", ShaderBindingKind::CBV), viewAllocation.gpuAddress);
			RootBindingCommand::SetComputeCBV(commands, pipeline.FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
			const auto srv = [&](const char* name, auto& buffer) {
				RootBindingCommand::SetComputeSRV(commands, pipeline.FindBindingByName(name, ShaderBindingKind::SRV), buffer.GetGPUAddress());
			};
			srv("gGPUTrails", instance.trails);
			srv("gGPUTrailPoints", instance.trailPoints);
			srv("gGPURibbonOrder", instance.ribbonOrder);
			RootBindingCommand::SetComputeUAV(commands, pipeline.FindBindingByName("gGPUTrailSegments", ShaderBindingKind::UAV),
				segments.GetGPUAddress(), segments.GetUAVGPUHandle());
			RootBindingCommand::SetComputeUAV(commands, pipeline.FindBindingByName("gGPUTrailArguments", ShaderBindingKind::UAV),
				arguments.GetGPUAddress(), arguments.GetUAVGPUHandle());
			commands->Dispatch(1, 1, 1);
			fixture.commands.UAVBarrierAll();
		}
		arguments.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		auto bytes = fixture.ReadResource(arguments.GetResource(), sizeof(ParticleUInt4GPU));
		ParticleUInt4GPU draw;
		std::memcpy(&draw, bytes.data(), sizeof(draw));
		if (draw.x != 6 || draw.y != 2) return false;
		segments.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		bytes = fixture.ReadResource(segments.GetResource(), 2 * sizeof(ParticleGPUViewResources::Key));
		std::unordered_set<uint32_t> found;
		for (uint32_t i = 0; i < 2; ++i) {
			ParticleGPUViewResources::Key segment;
			std::memcpy(&segment, bytes.data() + i * sizeof(segment), sizeof(segment));
			if (segment.particleIndex % instance.pointCapacity != 0 ||
				segment.particleIndex / instance.pointCapacity >= 2 || !found.insert(segment.particleIndex).second) return false;
		}
		return true;
	}

	bool CheckTrails(ParticleGPUFixture& fixture, const PipelineState& simulate,
		const PipelineState& trailPipeline, const PipelineState& prepare) {

		auto step = MakeStep(4);
		auto settings = std::make_shared<ParticleSystemSettings>(*step.definition->settings);
		auto& trail = settings->trail;
		trail.enabled = true;
		trail.keepAfterParticleDeath = true;
		trail.maxTrails = 2;
		trail.pointLifetime = 2.0f;
		ParticleDefinitionCompiler definitions;
		step.definition = definitions.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		fixture.Simulate(instance, simulate, step, 0);
		fixture.Simulate(instance, simulate, step, 5);
		fixture.Simulate(instance, simulate, step, 2);
		DispatchTrail(fixture, instance, trailPipeline, trail, 0, 0);
		for (uint32_t operation : {1u, 2u, 3u, 9u}) DispatchTrail(fixture, instance, trailPipeline, trail, operation, 0.01f);
		auto bytes = fixture.Read(instance.statistics, sizeof(ParticleStatisticsGPU));
		ParticleStatisticsGPU statistics;
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		if (statistics.trailCount != 2 || statistics.overflowCount != 2) return false;
		// 加算描画の可視セグメントが先頭へ詰められることを確認する
		if (!CheckTrailCompaction(fixture, instance, trail, prepare)) return false;
		step.spawnCount = 0;
		step.deltaTime = 0.6f;
		for (int frame = 0; frame < 2; ++frame) {
			fixture.Upload(instance, step);
			fixture.Simulate(instance, simulate, step, 1);
			fixture.Simulate(instance, simulate, step, 3);
			fixture.Simulate(instance, simulate, step, 2);
			for (uint32_t operation : {1u, 2u, 3u, 9u}) DispatchTrail(fixture, instance, trailPipeline, trail, operation, step.deltaTime);
		}
		bytes = fixture.Read(instance.statistics, sizeof(ParticleStatisticsGPU));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		if (statistics.particleCount != 0 || statistics.trailCount != 2) return false;
		fixture.Upload(instance, step);
		fixture.Simulate(instance, simulate, step, 3);
		DispatchTrail(fixture, instance, trailPipeline, trail, 1, 3.0f);
		DispatchTrail(fixture, instance, trailPipeline, trail, 9, 0);
		bytes = fixture.Read(instance.statistics, sizeof(ParticleStatisticsGPU));
		std::memcpy(&statistics, bytes.data(), sizeof(statistics));
		return statistics.trailCount == 0 && statistics.overflowCount == 2;
	}

	bool CheckRibbon(ParticleGPUFixture& fixture, const PipelineState& simulate,
		const PipelineState& trailPipeline, const PipelineState& select) {

		auto step = MakeStep(4097);
		auto settings = std::make_shared<ParticleSystemSettings>(*step.definition->settings);
		settings->trail.enabled = true;
		settings->trail.mode = ParticleTrailMode::Ribbon;
		settings->trail.maxPoints = 32;
		ParticleDefinitionCompiler definitions;
		step.definition = definitions.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		step.birthSequence = (uint64_t(19) << 32) + UINT32_MAX - 2048;
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		fixture.Simulate(instance, simulate, step, 0);
		fixture.Simulate(instance, simulate, step, 5);
		fixture.Simulate(instance, simulate, step, 2);
		DispatchTrail(fixture, instance, trailPipeline, settings->trail, 0, 0);
		instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		instance.statistics.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		uint32_t count = instance.capacity;
		uint32_t input = 1;
		uint32_t output = 0;
		bool first = true;
		while (true) {
			const bool final = count <= 2048;
			auto constants = MakeParticleGPUTrailConstants(settings->trail, instance.capacity);
			constants.operation = final ? (first ? 13u : 12u) : (first ? 10u : 11u);
			constants.reserved = count;
			const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), constants);
			instance.ribbonCandidates[input].Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			instance.ribbonCandidates[output].Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			auto* commands = fixture.commands.GetCommandList();
			commands->SetComputeRootSignature(select.GetRootSignature());
			commands->SetPipelineState(select.GetComputePipeline());
			RootBindingCommand::SetComputeCBV(commands, select.FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
			const auto srv = [&](const char* name, auto& buffer) {
				RootBindingCommand::SetComputeSRV(commands, select.FindBindingByName(name, ShaderBindingKind::SRV), buffer.GetGPUAddress());
			};
			const auto uav = [&](const char* name, auto& buffer) {
				RootBindingCommand::SetComputeUAV(commands, select.FindBindingByName(name, ShaderBindingKind::UAV), buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
			};
			srv("gGPUParticles", instance.particles);
			srv("gGPUParents", instance.parents);
			srv("gGPUStatistics", instance.statistics);
			srv("gGPURibbonCandidatesIn", instance.ribbonCandidates[input]);
			uav("gGPURibbonCandidatesOut", instance.ribbonCandidates[output]);
			uav("gGPUTrails", instance.trails);
			uav("gGPUTrailPoints", instance.trailPoints);
			uav("gGPURibbonOrder", instance.ribbonOrder);
			const uint32_t groups = (count + 2047) / 2048;
			commands->Dispatch(groups, 1, 1);
			fixture.commands.UAVBarrierAll();
			if (final) break;
			count = groups * instance.pointCapacity;
			std::swap(input, output);
			first = false;
		}
		instance.trailPoints.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto bytes = fixture.ReadResource(instance.trailPoints.GetResource(), 32 * sizeof(ParticleTrailPointGPU));
		for (uint32_t i = 0; i < 32; ++i) {
			ParticleTrailPointGPU point;
			std::memcpy(&point, bytes.data() + i * sizeof(point), sizeof(point));
			const uint64_t birth = uint64_t(point.birthHigh) << 32 | point.birthLow;
			if (birth != step.birthSequence + step.spawnCount - 32 + i) return false;
		}
		return true;
	}

	bool CheckView(ParticleGPUFixture& fixture, const PipelineState& pipeline, uint32_t phaseCount) {

		auto step = MakeStep(1003);
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		std::vector<ParticleAttributesGPU> particles(instance.capacity);
		uint32_t counts[2]{};
		for (uint32_t i = 0; i < instance.capacity; ++i) {
			auto& particle = particles[i];
			particle.alive = i % 5 != 0 ? 1u : 0u;
			particle.phase = i % phaseCount;
			particle.positionAge.z = float(i % 100);
			if (particle.alive && particle.positionAge.z >= 20 && particle.positionAge.z <= 80) ++counts[particle.phase];
		}
		StructuredInstanceBuffer<ParticleAttributesGPU> upload;
		upload.Init(fixture.device.Get(), &fixture.descriptors);
		upload.Upload(particles);
		instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_DEST);
		fixture.commands.GetCommandList()->CopyBufferRegion(instance.particles.GetResource(), 0,
			upload.GetResource(), 0, particles.size() * sizeof(ParticleAttributesGPU));
		instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		ParticleGPUViewResources view;
		for (auto& keys : view.keys) { keys.Init(fixture.device.Get(), &fixture.descriptors); keys.EnsureCapacity(instance.capacity); }
		for (auto* buffer : {&view.counts, &view.histogram, &view.histogramOffsets, &view.arguments}) buffer->Init(fixture.device.Get(), &fixture.descriptors);
		view.counts.EnsureCapacity(8);
		view.histogram.EnsureCapacity(4 * 16);
		view.histogramOffsets.EnsureCapacity(4 * 16);
		view.arguments.EnsureCapacity(10);
		// Cameraだけを変え、参照順とカリングを独立して求める
		for (bool reverse : {false, true}) {
			ParticleGPUViewConstants values;
			values.capacity = instance.capacity;
			values.phaseCount = phaseCount;
			values.indexCount = 6;
			values.groupCount = 4;
			values.sortEnabled = 1;
			values.geometryRadius = 0;
			values.frustum[0] = Vector4(0, 0, 1, -20);
			values.frustum[1] = Vector4(0, 0, -1, 80);
			if (reverse) { values.cameraForward.z = -1; values.cameraPosition.z = 100; }
			for (auto* buffer : {&view.counts, &view.histogram, &view.histogramOffsets, &view.arguments})
				buffer->Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			uint32_t input = 1;
			uint32_t output = 0;
			const auto dispatch = [&](uint32_t operation, uint32_t groups) {
				view.keys[input].Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				view.keys[output].Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				values.operation = operation;
				const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), values);
				BindParticleGPUViewCompute(fixture.commands.GetCommandList(), pipeline, allocation.gpuAddress, instance, view, input, output, false);
				fixture.commands.GetCommandList()->Dispatch(groups, 1, 1);
				fixture.commands.UAVBarrierAll();
			};
			dispatch(0, 4);
			dispatch(1, 4);
			dispatch(2, 1);
			for (uint32_t shift = 0; shift < 32; shift += 4) {
				std::swap(input, output);
				values.radixShift = shift;
				dispatch(3, 4);
				dispatch(4, 1);
				dispatch(5, 4);
			}
			dispatch(6, 1);
			auto bytes = fixture.Read(view.arguments, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * 2);
			for (uint32_t phase = 0; phase < phaseCount; ++phase) {
				D3D12_DRAW_INDEXED_ARGUMENTS argument;
				std::memcpy(&argument, bytes.data() + phase * sizeof(argument), sizeof(argument));
				if (argument.IndexCountPerInstance != 6 || argument.InstanceCount != counts[phase]) return false;
			}
			view.keys[0].Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			bytes = fixture.ReadResource(view.keys[0].GetResource(), (counts[0] + counts[1]) * sizeof(ParticleGPUViewResources::Key));
			uint32_t previousPhase = 0;
			float previousDepth = 1e30f;
			std::unordered_set<uint32_t> found;
			for (uint32_t i = 0; i < counts[0] + counts[1]; ++i) {
				ParticleGPUViewResources::Key key;
				std::memcpy(&key, bytes.data() + i * sizeof(key), sizeof(key));
				if (key.particleIndex >= particles.size() || !found.insert(key.particleIndex).second) return false;
				const auto& particle = particles[key.particleIndex];
				if (!particle.alive || particle.positionAge.z < 20 || particle.positionAge.z > 80 || particle.phase < previousPhase) return false;
				const float depth = reverse ? 100 - particle.positionAge.z : particle.positionAge.z;
				if (particle.phase == previousPhase && depth > previousDepth) return false;
				previousDepth = depth;
				previousPhase = particle.phase;
			}
		}
		return true;
	}

	bool CheckFlipbook(ParticleGPUFixture& fixture, const PipelineState& pipeline) {

		auto step = MakeStep(1);
		auto settings = std::make_shared<ParticleSystemSettings>(*step.definition->settings);
		settings->phases[0].lifeEndMode = ParticleLifeEndMode::Clamp;
		settings->phases[0].modules = {{"Flipbook", {{"tilesX", {2, 4}}, {"tilesY", 2}, {"cycles", 1.0f}}}};
		ParticleDefinitionCompiler definitions;
		step.definition = definitions.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		fixture.Simulate(instance, pipeline, step, 0);
		fixture.Simulate(instance, pipeline, step, 5);
		fixture.Simulate(instance, pipeline, step, 2);
		step.spawnCount = 0;
		step.deltaTime = 0.25f;
		fixture.Simulate(instance, pipeline, step, 1);
		instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto bytes = fixture.ReadResource(instance.particles.GetResource(), sizeof(ParticleAttributesGPU));
		ParticleAttributesGPU sample;
		std::memcpy(&sample, bytes.data(), sizeof(sample));
		return sample.uv == Vector4(0.25f, 0.5f, 0.25f, 0.5f);
	}

	bool CheckDrawPipelines(ParticleGPUFixture& fixture) {

		const auto root = RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Particle/GPU";
		for (const char* vertex : {"particleGPUDraw.VS.hlsl", "particleGPUTrailDraw.VS.hlsl"}) {
			GraphicsPipelineDesc descriptor;
			descriptor.type = PipelineType::Vertex;
			descriptor.preRaster = {.file = Algorithm::PathToUTF8(root / vertex), .profile = "vs_6_0"};
			descriptor.pixel = {.file = Algorithm::PathToUTF8(root / "particleGPUDraw.PS.hlsl"), .profile = "ps_6_0"};
			descriptor.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			descriptor.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
			descriptor.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			descriptor.depthStencil.DepthEnable = FALSE;
			descriptor.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			descriptor.staticSamplers.push_back(CD3DX12_STATIC_SAMPLER_DESC(0));
			if (!PipelineStateBuilder::CreateGraphics(fixture.retirement, fixture.device.Get(), &fixture.compiler, descriptor)) return false;
		}
		return true;
	}
}

bool NEMTests::TestParticleGPUHardware() {

	ParticleGPUFixture fixture;
	auto simulate = fixture.MakePipeline("particleGPUSimulate.CS.hlsl");
	auto view = fixture.MakePipeline("particleGPUView.CS.hlsl");
	auto trail = fixture.MakePipeline("particleGPUTrailUpdate.CS.hlsl");
	auto ribbon = fixture.MakePipeline("particleGPURibbonSelect.CS.hlsl");
	auto prepare = fixture.MakePipeline("particleGPUTrailPrepare.CS.hlsl");
	return simulate && view && trail && ribbon && prepare &&
		RunTest("ParticleDrawPipelines", [&] { return CheckDrawPipelines(fixture); }) &&
		RunTest("ParticleCPUShapePipelines", [&] { return CheckParticleShapePipelines(fixture.device.Get(), fixture.retirement); }) &&
		RunTest("ParticleGPUView", [&] { return CheckView(fixture, *view, 1) && CheckView(fixture, *view, 2); }) &&
		RunTest("ParticleGPUFlipbook", [&] { return CheckFlipbook(fixture, *simulate); }) &&
		RunTest("ParticleGPUEmissive", [&] { return CheckParticleEmissive(fixture, *simulate); }) &&
		RunTest("ParticleGPUModuleParity", [&] { return CheckParticleGPUModuleParity(fixture, *simulate); }) &&
		RunTest("GPUInclusiveScan", [&] { return CheckGPUInclusiveScan(fixture); }) &&
		RunTest("ParticleMeshSampling", [&] { return CheckParticleMeshSampling(fixture); }) &&
		RunTest("ParticleWorldGPU", [&] { return CheckParticleWorldGPU(fixture); }) &&
		RunTest("ParticleDepthSnapshot", [&] { return CheckParticleDepthSnapshot(fixture); }) &&
		RunTest("ParticleFeedback", [&] { return CheckParticleFeedback(fixture); }) &&
		RunTest("ParticleGPUSourceSpawn", [&] { return CheckParticleGPUSourceSpawn(fixture); }) &&
		RunTest("ParticleGPUPartialGroups", [&] {
			return CheckSimulation(fixture, *simulate, 255) && CheckSimulation(fixture, *simulate, 257);
		}) &&
		RunTest("ParticleGPU100k", [&] { return CheckSimulation(fixture, *simulate, 100000); }) &&
		RunTest("ParticleGPU1M", [&] { return CheckSimulation(fixture, *simulate, 1000000); }) &&
		RunTest("ParticleGPUTrails", [&] { return CheckTrails(fixture, *simulate, *trail, *prepare); }) &&
		RunTest("ParticleGPURibbon", [&] { return CheckRibbon(fixture, *simulate, *trail, *ribbon); }) && fixture.CheckMessages();
}
