#include "ParticleGPUEmissiveTests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/World/Systems/Effect/ParticleModuleExecution.h>

// c++
#include <algorithm>
#include <cmath>
#include <cstring>

bool NEMTests::CheckParticleEmissive(ParticleGPUFixture& fixture, const Engine::PipelineState& pipeline) {

	using namespace Engine;
	ParticleColorOverLifetimeModule color;
	auto colorSettings = color.GetSettings();
	colorSettings.startColor = Color4(0.2f, 0.4f, 0.8f, 0.1f);
	colorSettings.endColor = Color4(0.8f, 0.6f, 0.2f, 0.7f);
	colorSettings.easingType = EasingType::Linear;
	color.SetSettings(colorSettings);
	ParticleEmissiveModule emissive;
	auto emission = emissive.GetSettings();
	emission.startColor = Color3(0.9f, 0.1f, 0.3f);
	emission.endColor = Color3(0.1f, 0.5f, 0.7f);
	emission.startIntensity = 2.0f;
	emission.endIntensity = 6.0f;
	emission.colorEasingType = EasingType::Linear;
	emission.intensityEasingType = EasingType::EaseOutSine;
	ParticleDefinitionCompiler definitions;
	for (bool follow : {false, true}) for (bool reverse : {false, true}) {
		emission.useParticleColor = follow;
		emissive.SetSettings(emission);
		auto settings = std::make_shared<ParticleSystemSettings>(MakeDefaultParticleSystemSettings());
		settings->emitter.maxParticles = 1;
		settings->emitter.shape = ParticleEmitterShape::Point;
		settings->phases[0].lifetime = ParticleValue<float>{1.0f};
		settings->phases[0].lifeEndMode = ParticleLifeEndMode::Clamp;
		settings->phases[0].modules = {{"ColorOverLifetime", color.ToJson()}, {"Emissive", emissive.ToJson()}};
		if (reverse) std::reverse(settings->phases[0].modules.begin(), settings->phases[0].modules.end());
		ParticleGPUSimulationStep step;
		step.definition = definitions.Resolve(settings);
		step.program = std::make_shared<const ParticleGPUProgram>(CompileParticleGPUProgram(*step.definition));
		if (!step.program->unsupportedReason.empty()) return false;
		step.parents.resize(1);
		step.spawnCount = 1;
		step.seed = 412;
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
		float previousAge = 0.0f;
		// 発生直後と寿命の途中・終端でRGBと強度を別々に確認する
		for (float age : {0.0f, 0.23f, 0.67f, 1.0f}) {
			step.deltaTime = age - previousAge;
			if (step.deltaTime > 0.0f) {
				fixture.Upload(instance, step);
				fixture.Simulate(instance, pipeline, step, 1);
			}
			previousAge = age;
			instance.particles.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto bytes = fixture.ReadResource(instance.particles.GetResource(), sizeof(ParticleAttributesGPU));
			ParticleAttributesGPU actual;
			std::memcpy(&actual, bytes.data(), sizeof(actual));
			Particle expected;
			expected.age = age;
			ParticleModuleExecution::ApplyUpdateModules(expected, step.definition->phases[0], step.deltaTime);
			const Vector4 difference = actual.emissive - expected.emissive;
			if (!actual.alive || (std::max)({std::abs(difference.x), std::abs(difference.y), std::abs(difference.z),
				std::abs(difference.w)}) > 0.001f) return false;
		}
	}
	return fixture.CheckMessages();
}
