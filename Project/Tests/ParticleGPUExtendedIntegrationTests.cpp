#include "ParticleGPUExtendedIntegrationTests.h"
#include "ParticleGPUFeedbackFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Sources/ParticleGPUSourceResources.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleWorldModules.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace Engine;
using namespace NEMTests;

namespace {

	struct EmptySource {

		ParticleGPUFixture& fixture;
		StructuredInstanceBuffer<MeshVertex> vertices;
		StructuredInstanceBuffer<uint32_t> indices;
		StructuredInstanceBuffer<ParticleSourceSubMeshGPU> subMeshes;
		StructuredInstanceBuffer<float> distribution;
		StructuredInstanceBuffer<Vector4> colors;
		uint32_t image = UINT32_MAX;
		uint32_t depth = UINT32_MAX;

		// Analytic経路でもReflectionに残る入力を有効にする
		explicit EmptySource(ParticleGPUFixture& target) : fixture(target) {

			auto* device = fixture.device.Get();
			auto* descriptors = &fixture.descriptors;
			vertices.Init(device, descriptors); indices.Init(device, descriptors);
			subMeshes.Init(device, descriptors); distribution.Init(device, descriptors); colors.Init(device, descriptors);
			D3D12_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			view.Texture2D.MipLevels = 1;
			descriptors->CreateSRV(image, nullptr, view);
			view.Format = DXGI_FORMAT_R32_FLOAT;
			descriptors->CreateSRV(depth, nullptr, view);
		}

		~EmptySource() {

			fixture.descriptors.Retire(image, {});
			fixture.descriptors.Retire(depth, {});
		}

		// frame別の空領域と本番の定数配置を接続する
		void Bind(const PipelineState& pipeline, const ParticleGPUProgram& program) {

			const std::array<MeshVertex, 1> vertex{};
			const std::array<uint32_t, 1> index{};
			const std::array<float, 1> weight{};
			const std::array<Vector4, 1> color{Vector4(1, 1, 1, 1)};
			const std::array<ParticleSourceSubMeshGPU, 1> transform{};
			vertices.Upload(vertex); indices.Upload(index); distribution.Upload(weight);
			colors.Upload(color); subMeshes.Upload(transform);
			ParticleGPUSourceConstants values;
			values.stateSlot = program.sourceStateSlot;
			values.moduleStateCount = program.stateCount;
			const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), values);
			auto* commands = fixture.commands.GetCommandList();
			if (const auto* binding = pipeline.FindBindingByName("ParticleGPUSourceConstants", ShaderBindingKind::CBV))
				RootBindingCommand::SetComputeCBV(commands, binding, allocation.gpuAddress);
			const auto bind = [&](const char* name, const auto& buffer) {
				if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV))
					RootBindingCommand::SetComputeSRV(commands, binding, buffer.GetGPUAddress(), buffer.GetGPUHandle());
			};
			bind("gGPUSourceVertices", vertices); bind("gGPUSourcePreviousVertices", vertices);
			bind("gGPUSourceIndices", indices); bind("gGPUSourceVertexSubMeshes", indices);
			bind("gGPUSourceSubMeshes", subMeshes); bind("gGPUSourceDistribution", distribution);
			bind("gGPUSourceVertexColors", colors);
			const char* names[]{"gGPUSourceColor", "gGPUSourceDepth", "gGPUSourceMask", "gGPUSourcePreviousDepth"};
			for (uint32_t item = 0; item < 4; ++item) {
				if (const auto* binding = pipeline.FindBindingByName(names[item], ShaderBindingKind::SRV))
					RootBindingCommand::SetComputeSRV(commands, binding, 0, fixture.descriptors.GetGPUHandle(item & 1u ? depth : image));
			}
		}
	};

	// 実行定義を通常のCompilerで作る
	ParticleGPUSimulationStep MakeStep(bool source) {

		auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
		settings->emitter.maxParticles = 3;
		settings->emitter.shape = ParticleEmitterShape::Point;
		settings->emitter.speed = ParticleValue<float>{0.0f};
		settings->emitter.emitOffset = ParticleValue<Vector3>{Vector3(0, 1, 0)};
		settings->phases[0].lifetime = ParticleValue<float>{5.0f};
		ParticleColorOverLifetimeModule color;
		auto colors = color.GetSettings();
		colors.startColor = colors.endColor = source ? Color4(0.25f, 0.5f, 0.75f, 1) : Color4::White();
		color.SetSettings(colors);
		settings->phases[0].modules = {{"ColorOverLifetime", color.ToJson()}};
		if (source) {
			ParticleForceFieldModule force;
			ParticleForceFieldSettings forceSettings;
			forceSettings.strength = 1;
			force.SetSettings(forceSettings);
			ParticleCollisionModule collision;
			ParticleCollisionSettings collisionSettings;
			collisionSettings.enabled = collisionSettings.plane = true;
			collisionSettings.sceneColliders = false;
			collisionSettings.bounce = 1;
			collision.SetSettings(collisionSettings);
			settings->phases[0].modules.push_back({"ForceField", force.ToJson()});
			settings->phases[0].modules.push_back({"Collision", collision.ToJson()});
		} else {
			settings->phases[0].lifetime = ParticleValue<float>{0.05f};
			settings->phases[0].lifeEndMode = ParticleLifeEndMode::Advance;
			auto& phase = settings->phases.emplace_back();
			phase.lifetime = ParticleValue<float>{5.0f};
			phase.modules = {{"ColorOverLifetime", color.ToJson()}};
		}
		ParticleDefinitionCompiler compiler;
		ParticleGPUSimulationStep step;
		step.definition = compiler.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		step.parents.resize(settings->phases.size());
		step.seed = 643;
		step.spawnCount = source ? 1u : 0u;
		step.birthSequence = source ? 0 : 1000;
		return step;
	}

	// 統計だけの検証ではなく本番の属性を書き戻す
	bool CheckMain(ParticleGPUFixture& fixture, const PipelineState& simulate, const PipelineState& prepare) {

		ParticleGPUFeedbackFixture feedback(fixture);
		EmptySource emptySource(fixture);
		ParticleGPUWorldResources world;
		world.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement);
		auto snapshot = std::make_shared<ParticleGPUWorldSnapshot>();
		auto& wind = snapshot->forces.emplace_back();
		wind.parameters = Vector4(8, 100, 0, 3);
		wind.axis = Vector4(0, -1, 0, 0);
		wind.metadata[0] = UINT32_MAX;
		snapshot->constants.counts[0] = 1;
		auto sourceStep = MakeStep(true), childStep = MakeStep(false);
		if (!sourceStep.program->unsupportedReason.empty() || !childStep.program->unsupportedReason.empty()) {
			std::cerr << "Extended program source=" << sourceStep.program->unsupportedReason << " child=" << childStep.program->unsupportedReason << '\n';
			return false;
		}
		auto sourceState = std::make_shared<ParticleGPUState>(), childState = std::make_shared<ParticleGPUState>();
		sourceState->definition = sourceStep.definition; sourceState->program = sourceStep.program;
		childState->definition = childStep.definition; childState->program = childStep.program;
		ParticleGPUInstance source, child;
		source.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, sourceState);
		child.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, childState);
		feedback.values.reserved = 1;
		feedback.route.count = 1;
		feedback.Dispatch(prepare, 3); feedback.Dispatch(prepare, 0); feedback.Dispatch(prepare, 1);
		const auto dispatch = [&](ParticleGPUInstance& instance, const ParticleGPUSimulationStep& step, uint32_t operation) {
			fixture.Upload(instance, step);
			world.Upload(snapshot);
			instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			instance.statistics.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			instance.moduleStates.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			auto values = MakeParticleGPUSimulationConstants(step);
			values.operation = operation;
			const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), values);
			auto* commands = fixture.commands.GetCommandList();
			BindParticleGPUCompute(commands, simulate, allocation.gpuAddress, instance);
			world.Bind(commands, simulate);
			emptySource.Bind(simulate, *step.program);
			feedback.Bind(simulate, 0, false);
			commands->Dispatch(1, 1, 1);
			fixture.commands.UAVBarrierAll();
		};
		dispatch(source, sourceStep, 0); dispatch(source, sourceStep, 5); dispatch(source, sourceStep, 2);
		sourceStep.spawnCount = 0;
		sourceStep.deltaTime = 0.5f;
		dispatch(source, sourceStep, 1); dispatch(source, sourceStep, 1);
		StructuredRWBuffer<ParticleGPUEvent> previousEvents;
		StructuredRWBuffer<uint32_t> previousCounts;
		previousEvents.Init(fixture.device.Get(), &fixture.descriptors); previousEvents.EnsureCapacity(4);
		previousCounts.Init(fixture.device.Get(), &fixture.descriptors); previousCounts.EnsureCapacity(1);
		feedback.events.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		feedback.counts.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		previousEvents.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_DEST);
		previousCounts.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_DEST);
		auto* commands = fixture.commands.GetCommandList();
		commands->CopyBufferRegion(previousEvents.GetResource(), 0, feedback.events.GetResource(), 0, 4 * sizeof(ParticleGPUEvent));
		commands->CopyBufferRegion(previousCounts.GetResource(), 0, feedback.counts.GetResource(), 0, sizeof(uint32_t));
		previousEvents.Transition(fixture.commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		previousCounts.Transition(fixture.commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		feedback.values.source = feedback.identities[1];
		feedback.Dispatch(prepare, 1);
		feedback.Bind(prepare, 2);
		RootBindingCommand::SetComputeSRV(commands, prepare.FindBindingByName("gGPUPreviousEvents", ShaderBindingKind::SRV),
			previousEvents.GetGPUAddress(), previousEvents.GetSRVGPUHandle());
		RootBindingCommand::SetComputeSRV(commands, prepare.FindBindingByName("gGPUPreviousEventCounts", ShaderBindingKind::SRV),
			previousCounts.GetGPUAddress(), previousCounts.GetSRVGPUHandle());
		commands->Dispatch(1, 1, 1);
		fixture.commands.UAVBarrierAll();
		feedback.Dispatch(prepare, 0);
		feedback.values.routeCount = 1;
		feedback.route.kind = static_cast<uint32_t>(ParticleEventKind::Time);
		feedback.route.phase = 1;
		feedback.route.time = 0;
		dispatch(child, childStep, 0); dispatch(child, childStep, 5); dispatch(child, childStep, 2);
		childStep.deltaTime = 0.1f;
		dispatch(child, childStep, 1); dispatch(child, childStep, 1);
		child.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto bytes = fixture.ReadResource(child.particles.GetResource(), 3 * sizeof(ParticleAttributesGPU));
		std::array<ParticleAttributesGPU, 3> results;
		std::memcpy(results.data(), bytes.data(), bytes.size());
		uint32_t childIndex = 3;
		for (uint32_t index = 0; index < results.size(); ++index) {
			if (results[index].alive) {
				if (childIndex != 3) { std::cerr << "Extended: multiple child particles\n"; return false; }
				childIndex = index;
			}
		}
		if (childIndex == 3) {
			const auto countData = fixture.Read(previousCounts, sizeof(uint32_t));
			uint32_t count;
			std::memcpy(&count, countData.data(), sizeof(count));
			source.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto sourceData = fixture.ReadResource(source.particles.GetResource(), 3 * sizeof(ParticleAttributesGPU));
			std::array<ParticleAttributesGPU, 3> sourceResults;
			std::memcpy(sourceResults.data(), sourceData.data(), sourceData.size());
			std::cerr << "Extended: no child previousEvents=" << count << '\n';
			for (const auto& item : sourceResults) {
				std::cerr << "  source alive=" << item.alive << " y=" << item.positionAge.y << " vy=" << item.velocityLifetime.y << '\n';
			}
			return false;
		}
		const auto& result = results[childIndex];
		if (!result.alive || result.phase != 1 || result.birthLow != 1000 || result.birthHigh != 0 || result.color != Vector4(0.25f, 0.5f, 0.75f, 1) ||
			result.velocityLifetime.y >= -7.9f || std::abs(result.positionAge.w - 0.1f) > 1e-6f) {
			std::cerr << "Extended child alive=" << result.alive << " phase=" << result.phase << " birth=" << result.birthLow << ":" << result.birthHigh
				<< " age=" << result.positionAge.w << " vy=" << result.velocityLifetime.y << " color=" << result.color.x << "," << result.color.y
				<< "," << result.color.z << "," << result.color.w << '\n';
			return false;
		}
		feedback.inherited.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto inheritedBytes = fixture.ReadResource(feedback.inherited.GetResource(), 3 * sizeof(ParticleGPUInheritedState));
		ParticleGPUInheritedState inherited;
		std::memcpy(&inherited, inheritedBytes.data() + childIndex * sizeof(inherited), sizeof(inherited));
		feedback.children.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto eventBytes = fixture.ReadResource(feedback.children.GetResource(), sizeof(ParticleGPUEvent));
		ParticleGPUEvent inheritedEvent;
		std::memcpy(&inheritedEvent, eventBytes.data(), sizeof(inheritedEvent));
		const auto countBytes = fixture.Read(feedback.counts, sizeof(uint32_t));
		uint32_t eventCount;
		std::memcpy(&eventCount, countBytes.data(), sizeof(eventCount));
		if (eventCount != 1) { std::cerr << "Extended phase time0 count=" << eventCount << '\n'; return false; }
		const bool valid = (inherited.flags & 280u) == 280u && inherited.seed == inheritedEvent.seed &&
			inherited.color == Vector4(0.25f, 0.5f, 0.75f, 1);
		if (!valid) std::cerr << "Extended inherited flags=" << inherited.flags << " seed=" << inherited.seed << " expected=" << inheritedEvent.seed << '\n';
		return valid;
	}
}

bool NEMTests::CheckParticleGPUExtendedIntegration(ParticleGPUFixture& fixture) {

	auto simulate = fixture.MakePipeline("particleGPUExtendedSimulate.CS.hlsl");
	auto prepare = fixture.MakePipeline("particleGPUEventPrepare.CS.hlsl");
	if (!simulate || !prepare) return false;
	for (uint32_t count : {1u, 2u}) {
		fixture.ChangeFrameCount(count);
		if (!CheckMain(fixture, *simulate, *prepare)) {
			std::cerr << "Extended integration frame contexts=" << count << '\n';
			return false;
		}
	}
	return fixture.CheckMessages();
}
