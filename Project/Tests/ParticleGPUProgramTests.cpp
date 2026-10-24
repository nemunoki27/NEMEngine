#include "ParticleSystemRuntimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUProgram.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUTrailTypes.h>
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/World/Systems/Effect/ParticleInstanceUpdater.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>

// c++
#include <cmath>
#include <bit>

using namespace Engine;

bool NEMTests::TestParticleGPUProgram() {

	ParticleDefinitionCompiler compiler;
	// 追加候補の判定と実際のGPU変換結果を照合する
	for (const auto& descriptor : ParticleModuleRegistry::GetInstance().GetDescriptors()) {
		auto candidate = MakeDefaultParticleSystemSettings();
		candidate.phases[0].modules = {{ descriptor.id, descriptor.create()->ToJson() }};
		const auto converted = CompileParticleGPUProgram(*compiler.Resolve(std::make_shared<const ParticleSystemSettings>(candidate)));
		if (IsParticleGPUModuleSupported(descriptor.id) != converted.unsupportedReason.empty()) return false;
	}
	if (IsParticleGPUModuleSupported("UnknownModule")) return false;
	auto settings = MakeDefaultParticleSystemSettings();
	auto program = CompileParticleGPUProgram(*compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings)));
	if (!program.unsupportedReason.empty() || program.instructions.size() != 2 || program.curves.empty()) return false;
	// CPU評価とGPUの共有テーブルを異なる標本位置で照合する
	ParticleColorOverLifetimeModule color;
	const auto& instruction = program.instructions[1];
	for (int i = 0; i < 1003; ++i) {
		Particle particle;
		particle.age = float(i) / 1002;
		color.OnUpdate(particle, 0.0f);
		const float position = particle.age * (instruction.curveCount - 1);
		const uint32_t index = (std::min)(uint32_t(position), instruction.curveCount - 2);
		const float t = position - index;
		const auto sampled = program.curves[instruction.curveOffset + index].value0 * (1 - t) +
			program.curves[instruction.curveOffset + index + 1].value0 * t;
		if (std::abs(particle.color.a - sampled.w) > 0.001f) return false;
	}
	settings.phases[0].modules.push_back({"ShapeOverLifetime", nlohmann::json::object()});
	program = CompileParticleGPUProgram(*compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings)));
	if (program.unsupportedReason.empty()) return false;
	ECSWorld world(ECSWorldKind::Runtime);
	ParticleInstanceUpdater updater;
	ParticleInstanceState instance;
	updater.UpdateInstance(world, instance, std::make_shared<const ParticleSystemSettings>(settings), Matrix4x4::Identity(), {},
		true, 0.01f, true, true, false, ParticleSimulationTarget::GPU);
	if (!instance.emitter.gpu || !instance.emitter.gpu->rejected || !instance.emitter.particles.empty()) return false;

	// 等間隔の標本間に収まる鋭い山も、無視せず精度不足として返す
	auto params = color.ToJson();
	params["useCurve"] = true;
	params["curveChannels"][0]["keys"] = nlohmann::json::array({
		{{"time", 0.0f}, {"value", 0.0f}}, {{"time", 0.312340f}, {"value", 0.0f}},
		{{"time", 0.312345f}, {"value", 1.0f}}, {{"time", 0.312350f}, {"value", 0.0f}}, {{"time", 1.0f}, {"value", 0.0f}}});
	settings = MakeDefaultParticleSystemSettings();
	settings.phases[0].modules = {{"ColorOverLifetime", params}};
	program = CompileParticleGPUProgram(*compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings)));
	if (program.unsupportedReason.empty()) return false;

	ParticleTrailSettings trail;
	trail.maxPoints = 127;
	trail.maxTrails = 1000000;
	trail.memoryBudgetMB = 3;
	const auto capacity = MakeParticleGPUTrailConstants(trail, 1000000);
	const auto actual = std::bit_ceil(uint64_t(capacity.trailCapacity) * capacity.pointCapacity) * sizeof(ParticleTrailPointGPU);
	return actual <= 3 * 1024 * 1024 && capacity.trailCapacity > 0;
}
