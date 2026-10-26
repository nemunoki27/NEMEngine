#include "ParticleSystemRuntimeTests.h"
#include "TestRunner.h"
#include "ParticleSourceTests.h"
#include "ParticleWorldTests.h"
#include "ParticleSurfaceTests.h"
#include "ParticleFeedbackTests.h"
#include "ParticleMeshColorTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUProgram.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUTrailTypes.h>
#include <Engine/Core/World/Systems/Effect/ParticleDefinitionCompiler.h>
#include <Engine/Core/World/Systems/Effect/ParticleInstanceUpdater.h>
#include <Engine/Core/World/Systems/Effect/ParticleModuleExecution.h>
#include <Engine/Core/Rendering/Particle/ParticleSimulationValidation.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Foundation/Math/Noise.h>

// c++
#include <algorithm>
#include <cmath>
#include <bit>

using namespace Engine;

namespace {

	// 保存値を残し、CPUとGPUと描画の全てから無効なModuleを除外する
	bool CheckModuleEnabled() {

		auto settings = MakeDefaultParticleSystemSettings();
		auto& modules = settings.phases[0].modules;
		const auto red = Color4(1, 0, 0, 1).ToJson();
		modules = {{"GravityForce", {{"gravity", Vector3(0, -4, 0).ToJson()}}},
			{"ColorOverLifetime", {{"startColor", red}, {"endColor", red}}},
			{"Rotation", {{"mode", "Additive"}}}, {"SizeOverLifetime", {{"startScale", 2.0f}, {"endScale", 2.0f}}},
			{"ShapeOverLifetime", nlohmann::json::object()}, {"CustomData", {{"enabled", true}}},
			{"CustomShaderParameter", nlohmann::json::object()},
			{"ForceField", nlohmann::json::object()}};
		modules[6].params["parameters"]["DisabledValue"]["constant"] = {1, 2, 3, 4};
		for (size_t index = 0; index < modules.size(); ++index) modules[index].enabled = index == 1 || index == 3;
		const auto saved = ToJson(settings);
		ParticleSystemSettings restored;
		if (!FromJson(saved, restored) || ToJson(restored) != saved) return false;
		if (!ValidateParticleSimulationSettings(restored, ParticleSimulationTarget::CPU).empty()) return false;
		ParticleDefinitionCompiler compiler;
		const auto definition = compiler.Resolve(std::make_shared<const ParticleSystemSettings>(restored));
		const auto& phase = definition->phases[0];
		if (phase.modules.size() != 2 || definition->stateCount != 0 || definition->renderSettings.shapeOverLifetime ||
			definition->renderSettings.phaseCustomData[0].enabled || !definition->renderSettings.phaseMaterialSettings[0].parameters.empty()) return false;
		const auto program = CompileParticleGPUProgram(*definition);
		if (!program.unsupportedReason.empty() || program.instructions.size() != 2 || program.stateCount != 0 || program.usesWorldInputs ||
			program.instructions[0].operation != ParticleGPUOperation::Color || program.instructions[1].operation != ParticleGPUOperation::Size) return false;
		Particle particle;
		particle.age = 0.25f;
		std::span<Particle> particles(&particle, 1);
		ParticleModuleExecution::ExecuteSpawnModules(particles, phase);
		ParticleModuleExecution::ExecuteUpdateModules(particles, phase, 0.5f);
		if (particle.velocity != Vector3{} || particle.color != Color4(1, 0, 0, 1) || particle.size != 2.0f) return false;
		// 再び有効にすると同じ設定値の重力だけが戻る
		restored.phases[0].modules[0].enabled = true;
		const auto enabled = compiler.Resolve(std::make_shared<const ParticleSystemSettings>(restored));
		if (enabled->structure == definition->structure || enabled->phases[0].modules.size() != 3) return false;
		ParticleModuleExecution::ExecuteUpdateModules(particles, enabled->phases[0], 0.5f);
		if (particle.velocity != Vector3(0, -2, 0)) return false;
		// フラグのない従来データは全て有効として読み込む
		auto legacy = saved;
		for (auto& module : legacy["phases"][0]["modules"]) module.erase("enabled");
		if (!FromJson(legacy, restored)) return false;
		return std::all_of(restored.phases[0].modules.begin(), restored.phases[0].modules.end(), [](const auto& module) { return module.enabled; });
	}

	// 勾配の解析式を中心差分と照合する
	bool CheckNoiseGradient() {

		constexpr float kStep = 0.001f;
		const Vector3 axes[]{Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)};
		for (uint32_t i = 0; i < 100; ++i) {
			const Vector3 p(std::sin(i * 0.41f) * 5.7f, std::cos(i * 0.72f) * 3.2f, i * 0.097f - 5.0f);
			const Vector3 gradient = Math::PerlinNoiseGradient3D(p);
			for (const Vector3& axis : axes) {
				const Vector3 a = p + axis * kStep, b = p - axis * kStep;
				const float numeric = (Math::PerlinNoise3D(a.x, a.y, a.z) - Math::PerlinNoise3D(b.x, b.y, b.z)) / (2 * kStep);
				if (std::abs(numeric - Vector3::Dot(gradient, axis)) > 0.003f) return false;
			}
		}
		return Math::CurlNoiseVector3(Vector3(1, 2, 3), 0) == Vector3{};
	}
}

bool NEMTests::TestParticleGPUProgram() {

	if (!RunTest("ParticleModuleEnabled", CheckModuleEnabled)) return false;
	if (!RunTest("CheckNoiseGradient", CheckNoiseGradient)) return false;
	if (!RunTest("TestParticleSourceReferences", TestParticleSourceReferences)) return false;
	if (!RunTest("TestParticleWorldData", TestParticleWorldData)) return false;
	if (!RunTest("TestParticleSurfaceRendering", TestParticleSurfaceRendering)) return false;
	if (!RunTest("TestParticleFeedbackSettings", TestParticleFeedbackSettings)) return false;
	if (!RunTest("TestParticleMeshVertexColors", TestParticleMeshVertexColors)) return false;
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
	if (!program.unsupportedReason.empty() || program.shapeStateSlot == UINT32_MAX || program.stateCount != 5) return false;
	settings.trail.enabled = true;
	settings.trail.maxPoints = 1025;
	ECSWorld world(ECSWorldKind::Runtime);
	ParticleInstanceUpdater updater;
	ParticleInstanceState instance;
	updater.UpdateInstance(world, instance, std::make_shared<const ParticleSystemSettings>(settings), Matrix4x4::Identity(), {},
		true, 0.01f, true, true, false, ParticleSimulationTarget::GPU);
	if (!instance.emitter.gpu || !instance.emitter.gpu->rejected || !instance.emitter.particles.empty()) return false;

	// 等間隔の標本間に収まる鋭い山も時刻を保って表す
	auto params = color.ToJson();
	params["useCurve"] = true;
	params["curveChannels"][0]["keys"] = nlohmann::json::array({
		{{"time", 0.0f}, {"value", 0.0f}}, {{"time", 0.312340f}, {"value", 0.0f}},
		{{"time", 0.312345f}, {"value", 1.0f}}, {{"time", 0.312350f}, {"value", 0.0f}}, {{"time", 1.0f}, {"value", 0.0f}}});
	settings = MakeDefaultParticleSystemSettings();
	settings.phases[0].modules = {{"ColorOverLifetime", params}};
	program = CompileParticleGPUProgram(*compiler.Resolve(std::make_shared<const ParticleSystemSettings>(settings)));
	if (!program.unsupportedReason.empty() || program.instructions.size() != 1) return false;
	const auto peak = std::find_if(program.curves.begin(), program.curves.end(), [](const auto& sample) {
		return sample.timing.x == 0.312345f && sample.timing.y != 0;
	});
	if (peak == program.curves.end() || std::abs(peak->value0.x - 1.0f) > 0.001f) return false;

	ParticleTrailSettings trail;
	trail.maxPoints = 127;
	trail.maxTrails = 1000000;
	trail.memoryBudgetMB = 3;
	const auto capacity = MakeParticleGPUTrailConstants(trail, 1000000);
	const auto actual = std::bit_ceil(uint64_t(capacity.trailCapacity) * capacity.pointCapacity) * sizeof(ParticleTrailPointGPU);
	return actual <= 3 * 1024 * 1024 && capacity.trailCapacity > 0;
}
