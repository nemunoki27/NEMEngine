#include "ParticleGPUModuleParityTests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeDataUtility.h>
#include <Engine/Core/World/Systems/Effect/ParticleModuleExecution.h>

// c++
#include <cmath>
#include <cstring>
#include <iostream>
#include <string_view>

using namespace Engine;

namespace {

	// 形状を指定しないPhaseで基本形状と直前の形状を保持する
	bool CheckShapeInheritance(NEMTests::ParticleGPUFixture& fixture, const PipelineState& pipeline, PrimitiveType shape) {

		auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
		settings->shape = shape;
		settings->ring.outerRadius = 2.0f;
		settings->emitter.maxParticles = 1;
		settings->emitter.shape = ParticleEmitterShape::Point;
		settings->phases.resize(3);
		for (auto& phase : settings->phases) phase.modules.clear();
		settings->phases[0].lifetime = ParticleValue<float>{0.125f};
		settings->phases[1].lifetime = ParticleValue<float>{0.25f};
		settings->phases[0].lifeEndMode = settings->phases[1].lifeEndMode = ParticleLifeEndMode::Advance;
		settings->phases[2].lifetime = ParticleValue<float>{1.0f};
		settings->phases[2].lifeEndMode = ParticleLifeEndMode::Clamp;
		settings->phases[1].modules = {{"ShapeOverLifetime", {{"shape", shape == PrimitiveType::Ring ? "Ring" : "Cylinder"}}}};
		ParticleDefinitionCompiler compiler;
		ParticleGPUSimulationStep step;
		step.definition = compiler.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		if (!step.program->unsupportedReason.empty() || step.program->shapeStateSlot == UINT32_MAX) return false;
		step.parents.resize(3);
		step.spawnCount = 1;
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		fixture.Simulate(instance, pipeline, step, 0);
		fixture.Simulate(instance, pipeline, step, 5);
		fixture.Simulate(instance, pipeline, step, 2);
		Particle expected;
		expected.lifetime = 0.125f;
		expected.shapeData = MakeParticleShapeData(step.definition->renderSettings);
		for (uint32_t frame = 0; frame < 6; ++frame) {
			instance.moduleStates.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto bytes = fixture.ReadResource(instance.moduleStates.GetResource(), 5 * sizeof(Vector4));
			ParticleShapeData actual;
			std::memcpy(&actual, bytes.data(), sizeof(actual));
			if (actual.params0 != expected.shapeData.params0 || actual.params1 != expected.shapeData.params1 ||
				actual.topColor != expected.shapeData.topColor || actual.centerColor != expected.shapeData.centerColor ||
				actual.bottomColor != expected.shapeData.bottomColor) return false;
			expected.age += 0.125f;
			const uint32_t previousPhase = expected.phaseIndex;
			if (expected.age >= expected.lifetime && !ParticleModuleExecution::AdvancePhaseOnLifeEnd(expected, step.definition->phases))
				return false;
			const auto& phase = step.definition->phases[expected.phaseIndex];
			if (expected.phaseIndex != previousPhase) ParticleModuleExecution::ApplySpawnModules(expected, phase);
			ParticleModuleExecution::ApplyUpdateModules(expected, phase, 0.125f);
			step.spawnCount = 0;
			step.deltaTime = 0.125f;
			// 読戻しで進んだframeの共有Bufferも本番と同じように更新する
			fixture.Upload(instance, step);
			fixture.Simulate(instance, pipeline, step, 1);
		}
		return true;
	}

	// 見た目だけでなく積分後の位置と速度も照合する
	bool CheckModule(NEMTests::ParticleGPUFixture& fixture, const PipelineState& pipeline,
		const std::string& id, const nlohmann::json& parameters, bool enabled = true) {

		auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
		settings->emitter.maxParticles = 1;
		settings->emitter.shape = ParticleEmitterShape::Point;
		settings->emitter.speed.constant = 1.0f;
		settings->emitter.point.direction = Vector3(0, 1, 0);
		settings->phases[0].lifetime.constant = 2.0f;
		settings->phases[0].parentSettings.useEmitter = false;
		settings->phases[0].modules = {{id, parameters}};
		settings->phases[0].modules[0].enabled = enabled;
		ParticleDefinitionCompiler compiler;
		ParticleGPUSimulationStep step;
		step.definition = compiler.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		if (!step.program->unsupportedReason.empty()) {
			std::cerr << id << ": " << step.program->unsupportedReason << '\n';
			return false;
		}
		step.parents.resize(1);
		step.spawnCount = 1;
		step.birthSequence = 137;
		step.seed = 41;
		auto state = std::make_shared<ParticleGPUState>();
		state->definition = step.definition;
		state->program = step.program;
		ParticleGPUInstance instance;
		instance.Init(fixture.device.Get(), fixture.descriptors, fixture.retirement, state);
		fixture.Upload(instance, step);
		fixture.Simulate(instance, pipeline, step, 0);
		fixture.Simulate(instance, pipeline, step, 5);
		fixture.Simulate(instance, pipeline, step, 2);

		// CPU側も発生時と最初の見た目を同じ順序で適用する
		auto module = ParticleModuleRegistry::GetInstance().Create(id);
		module->FromJson(parameters);
		module->SetStateSlot(0);
		Vector3 rotationSpeed{};
		Particle expected;
		expected.id = 137;
		expected.velocity = Vector3(0, 1, 0);
		expected.spawnDirection = expected.velocity;
		expected.lifetime = 2.0f;
		expected.rotationSpeeds = std::span<Vector3>(&rotationSpeed, 1);
		if (enabled) {
			module->OnSpawn(expected);
			module->OnUpdate(expected, 0.0f);
		}
		step.spawnCount = 0;
		step.deltaTime = 0.01f;
		for (uint32_t frame = 0; frame < 53; ++frame) {
			expected.previousAge = expected.age;
			expected.age += step.deltaTime;
			expected.pos += expected.velocity * step.deltaTime;
			if (enabled) module->OnUpdate(expected, step.deltaTime);
			fixture.Simulate(instance, pipeline, step, 1);
		}
		instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
		const auto bytes = fixture.ReadResource(instance.particles.GetResource(), sizeof(ParticleAttributesGPU));
		ParticleAttributesGPU actual;
		std::memcpy(&actual, bytes.data(), sizeof(actual));
		const auto close = [](float a, float b) { return std::isfinite(a) && std::abs(a - b) < 0.003f; };
		const bool valid = close(actual.positionAge.x, expected.pos.x) && close(actual.positionAge.y, expected.pos.y) &&
			close(actual.positionAge.z, expected.pos.z) && close(actual.velocityLifetime.x, expected.velocity.x) &&
			close(actual.velocityLifetime.y, expected.velocity.y) && close(actual.velocityLifetime.z, expected.velocity.z) &&
			close(actual.uv.x, expected.uvOffset.x) && close(actual.uv.y, expected.uvOffset.y) &&
			close(actual.color.x, expected.color.r) && close(actual.color.y, expected.color.g) &&
			close(actual.color.z, expected.color.b) && close(actual.color.w, expected.color.a) &&
			close(std::abs(actual.rotation.x * expected.rotation.x + actual.rotation.y * expected.rotation.y +
				actual.rotation.z * expected.rotation.z + actual.rotation.w * expected.rotation.w), 1.0f);
		if (!valid) std::cerr << "CPU/GPU Module mismatch: " << id << '\n';
		return valid;
	}
}

bool NEMTests::CheckParticleGPUModuleParity(ParticleGPUFixture& fixture, const PipelineState& pipeline) {

	auto curlPipeline = fixture.MakePipeline("particleGPUCurlSimulate.CS.hlsl");
	if (!curlPipeline) return false;
	for (const uint32_t frames : {1u, 2u}) {
		fixture.ChangeFrameCount(frames);
		// 無効なModuleは発生時にも更新時にも作用しない
		for (const char* id : {"GravityForce", "ColorOverLifetime", "Rotation"}) {
			const auto module = ParticleModuleRegistry::GetInstance().Create(id);
			if (!CheckModule(fixture, pipeline, id, module->ToJson(), false)) return false;
		}
		if (!CheckShapeInheritance(fixture, pipeline, PrimitiveType::Ring) ||
			!CheckShapeInheritance(fixture, pipeline, PrimitiveType::Cylinder)) return false;
		for (const char* id : {"NoiseForce", "NoiseUV", "SpiralMovement", "PendulumMovement", "CurlNoise"}) {
			auto module = ParticleModuleRegistry::GetInstance().Create(id);
			const PipelineState& selected = std::string_view(id) == "CurlNoise" ? *curlPipeline : pipeline;
			if (!CheckModule(fixture, selected, id, module->ToJson())) return false;
		}
		// 繰り返しの段差と折り返しを実Shaderの時刻検索でも照合する
		for (const char* loopType : {"Repeat", "PingPong"}) {
			auto module = ParticleModuleRegistry::GetInstance().Create("ColorOverLifetime");
			auto settings = module->ToJson();
			settings["loop"] = {{"loopCount", 3}, {"type", loopType}};
			settings["startColor"] = Color4(0.1f, 0.2f, 0.3f, 1).ToJson();
			settings["endColor"] = Color4(0.9f, 0.7f, 0.8f, 0).ToJson();
			if (!CheckModule(fixture, pipeline, "ColorOverLifetime", settings)) return false;
		}
		for (const char* valueType : {"Euler", "Quaternion"}) {
			for (const char* mode : {"Interpolate", "Additive"}) {
				auto module = ParticleModuleRegistry::GetInstance().Create("Rotation");
				auto settings = module->ToJson();
				settings["mode"] = mode;
				settings["valueType"] = valueType;
				settings["speedMode"] = "OverLifetime";
				settings["startSpeed"] = Vector3(35, 80, -20).ToJson();
				settings["endSpeed"] = Vector3(150, -25, 60).ToJson();
				settings["startQuaternionSpeed"] = 70.0f;
				settings["endQuaternionSpeed"] = 145.0f;
				settings["endAngle"] = Vector3(100, 35, -60).ToJson();
				settings["endQuaternionAngle"] = 140.0f;
				if (!CheckModule(fixture, pipeline, "Rotation", settings)) return false;
			}
		}
	}
	return fixture.CheckMessages();
}
