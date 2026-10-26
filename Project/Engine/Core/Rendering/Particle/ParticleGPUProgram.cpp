#include "ParticleGPUProgram.h"
#include "ParticleGPUCurveCompiler.h"
#include "ParticleGPUMovementCompiler.h"
#include "ParticleGPUShapeCompiler.h"
#include "ParticleGPUWorldCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleGravityForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleFlipbookModule.h>

// c++
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace Engine;

namespace {

	// 実行とInspectorで同じModuleの対応表を使う
	constexpr std::pair<std::string_view, ParticleGPUOperation> kModuleOperations[] = {
		{ "ColorOverLifetime", ParticleGPUOperation::Color }, { "SizeOverLifetime", ParticleGPUOperation::Size },
		{ "ScaleOverLifetime", ParticleGPUOperation::Scale }, { "Emissive", ParticleGPUOperation::Emissive },
		{ "AlphaReference", ParticleGPUOperation::AlphaReference }, { "ColorUV", ParticleGPUOperation::UV },
		{ "GravityForce", ParticleGPUOperation::Gravity }, { "Rotation", ParticleGPUOperation::Rotation },
		{ "LookToVelocity", ParticleGPUOperation::LookToVelocity }, { "Flipbook", ParticleGPUOperation::Flipbook },
		{ "NoiseForce", ParticleGPUOperation::NoiseForce }, { "NoiseUV", ParticleGPUOperation::NoiseUV },
		{ "SpiralMovement", ParticleGPUOperation::Spiral }, { "PendulumMovement", ParticleGPUOperation::Pendulum },
		{ "ShapeOverLifetime", ParticleGPUOperation::Shape }, { "CurlNoise", ParticleGPUOperation::CurlNoise },
		{ "ForceField", ParticleGPUOperation::ForceField }, { "VectorField", ParticleGPUOperation::VectorField },
		{ "SplineMovement", ParticleGPUOperation::Spline }, { "Collision", ParticleGPUOperation::Collision },
	};

	// Module名をGPUの命令へ対応付ける
	std::optional<ParticleGPUOperation> FindModuleOperation(std::string_view moduleID) {

		for (const auto& [id, operation] : kModuleOperations) {
			if (id == moduleID) return operation;
		}
		return std::nullopt;
	}

	template<typename T>
	T Minimum(const ParticleValue<T>& value) {

		return value.type == ParticleValueType::Constant ? value.constant : value.min;
	}

	template<typename T>
	T Maximum(const ParticleValue<T>& value) {

		return value.type == ParticleValueType::Constant ? value.constant : value.max;
	}

	Vector4 Pack(const Vector3& value, float w = 0.0f) {

		return Vector4(value.x, value.y, value.z, w);
	}

	ParticleGPUCurveSample Evaluate(IParticleModule& module, ParticleGPUOperation operation, float progress) {

		Particle particle;
		particle.age = progress;
		particle.lifetime = 1.0f;
		module.OnUpdate(particle, 0.0f);
		ParticleGPUCurveSample result;
		switch (operation) {
		case ParticleGPUOperation::Color:
			result.value0 = Vector4(particle.color.r, particle.color.g, particle.color.b, particle.color.a);
			break;
		case ParticleGPUOperation::Size: result.value0.x = particle.size; break;
		case ParticleGPUOperation::Scale: result.value0 = Pack(particle.scale); break;
		case ParticleGPUOperation::Emissive: result.value0 = particle.emissive; break;
		case ParticleGPUOperation::AlphaReference: result.value0.x = particle.alphaReference; break;
		case ParticleGPUOperation::UV:
			result.value0 = Vector4(particle.uvOffset.x, particle.uvOffset.y, particle.uvScale.x, particle.uvScale.y);
			result.value1 = Vector4(particle.uvRotation, particle.uvPivot.x, particle.uvPivot.y, 0);
			break;
		case ParticleGPUOperation::Rotation: {
			const auto& rotation = static_cast<ParticleRotationModule&>(module);
			if (rotation.GetSettings().mode == ParticleRotationMode::Additive) result.value0 = Pack(rotation.EvaluateSpeed(progress));
			else result.value0 = Vector4(particle.rotation.x, particle.rotation.y, particle.rotation.z, particle.rotation.w);
			break;
		}
		default: break;
		}
		return result;
	}


	ParticleGPUInstruction CompileRotation(const ParticleRotationModule& module) {

		const auto& settings = module.GetSettings();
		ParticleGPUInstruction result;
		result.operation = ParticleGPUOperation::Rotation;
		result.flags = settings.mode == ParticleRotationMode::Interpolate ? 4u :
			(settings.mode == ParticleRotationMode::Additive ? 1u : 0u);
		if (settings.speedMode == ParticleRotationSpeedMode::OverLifetime && (result.flags & 1u)) result.flags |= 8u;
		if (settings.valueType == ParticleRotationValueType::Euler) {
			const auto& angle = result.flags ? settings.addAngle : settings.fixedAngle;
			result.parameter0 = Pack(Minimum(angle));
			result.parameter1 = Pack(Maximum(angle));
			result.parameter2 = Pack(Minimum(settings.rotationSpeed));
			result.parameter3 = Pack(Maximum(settings.rotationSpeed));
		} else {
			result.flags |= 2u;
			const bool additive = (result.flags & 1u) != 0;
			const auto& angle = additive ? settings.addQuaternionAngle : settings.fixedQuaternionAngle;
			result.parameter0 = Pack(additive ? settings.addAxis : settings.fixedAxis, Minimum(angle));
			result.parameter1.x = Maximum(angle);
			result.parameter2 = Pack(settings.rotationSpeedAxis, Minimum(settings.rotationQuaternionSpeed));
			result.parameter3.x = Maximum(settings.rotationQuaternionSpeed);
		}
		return result;
	}

	ParticleGPUInstruction CompileInstruction(const std::string& id, IParticleModule& module, ParticleGPUProgram& program) {

		const auto operation = FindModuleOperation(id);
		if (!operation) throw std::invalid_argument("GPU未対応のModuleです: " + id);
		ParticleGPUInstruction instruction;
		instruction.operation = *operation;
		if (CompileParticleGPUWorldModule(module, instruction, program)) return instruction;
		if (*operation == ParticleGPUOperation::Emissive) {
			instruction.flags = static_cast<ParticleEmissiveModule&>(module).GetSettings().useParticleColor ? 1u : 0u;
		} else if (*operation == ParticleGPUOperation::UV) {
			const auto& settings = static_cast<ParticleColorUVModule&>(module).GetSettings();
			instruction.flags = settings.updateType == ParticleUVUpdateType::Scroll ? 1u : 0u;
			instruction.parameter0 = Vector4(settings.scrollSpeed.x, settings.scrollSpeed.y, 0, 0);
		} else if (*operation == ParticleGPUOperation::Gravity) {
			const auto& settings = static_cast<ParticleGravityForceModule&>(module).GetSettings();
			instruction.flags = settings.reflectGround ? 1u : 0u;
			instruction.parameter0 = Pack(settings.gravity, settings.reflectGroundY);
			instruction.parameter1.x = settings.restitution;
			return instruction;
		} else if (*operation == ParticleGPUOperation::Rotation) {
			instruction = CompileRotation(static_cast<ParticleRotationModule&>(module));
			if ((instruction.flags & 1u) && !(instruction.flags & 8u)) instruction.parameter3.w = static_cast<float>(program.stateCount++);
			if (instruction.flags & 12u) {
				CompileParticleGPUCurve(module.ToJson(), [&](float progress) {
					return Evaluate(module, instruction.operation, progress);
				}, instruction, program);
			}
			return instruction;
		}
		else if ((*operation >= ParticleGPUOperation::NoiseForce && *operation <= ParticleGPUOperation::Pendulum) ||
			*operation == ParticleGPUOperation::CurlNoise) {
			CompileParticleGPUMovement(module, instruction, program);
			return instruction;
		}
		else if (*operation == ParticleGPUOperation::LookToVelocity) {
			return instruction;
		} else if (*operation == ParticleGPUOperation::Flipbook) {
			const auto& settings = static_cast<ParticleFlipbookModule&>(module).GetSettings();
			uint64_t total = 0;
			for (const auto columns : settings.tilesX) total += columns;
			if (settings.tilesX.empty() || total > 16777216) throw std::invalid_argument("GPU Flipbookの総フレーム上限は16777216です");
			instruction.parameter0 = Vector4(float(settings.tilesX.front()), float(settings.tilesY), settings.cycles, float(total));
			instruction.flags = std::any_of(settings.tilesX.begin(), settings.tilesX.end(), [&](int32_t columns) { return columns != settings.tilesX.front(); }) ? 1u : 0u;
			if (instruction.flags) {
				instruction.curveOffset = static_cast<uint32_t>(program.curves.size());
				instruction.curveCount = static_cast<uint32_t>(settings.tilesX.size());
				uint32_t end = 0;
				for (const auto columns : settings.tilesX) {
					end += columns;
					ParticleGPUCurveSample row;
					row.value0 = Vector4(float(columns), float(end), 0, 0);
					program.curves.push_back(row);
				}
			}
			return instruction;
		}
		CompileParticleGPUCurve(module.ToJson(), [&](float progress) {
			return Evaluate(module, instruction.operation, progress);
		}, instruction, program);
		return instruction;
	}
}

bool Engine::IsParticleGPUModuleSupported(std::string_view moduleID) {

	return moduleID == "CustomData" || moduleID == "CustomShaderParameter" || FindModuleOperation(moduleID).has_value();
}

ParticleGPUProgram Engine::CompileParticleGPUProgram(const ParticleSystemDefinition& definition) {

	ParticleGPUProgram program;
	try {
		const auto& sourceSettings = definition.settings->source;
		if (sourceSettings.kind != ParticleSourceKind::Analytic) {
			program.sourceStateSlot = program.stateCount;
			program.stateCount += 2;
		}
		if (sourceSettings.kind == ParticleSourceKind::CameraSurface &&
			(sourceSettings.attachToSurface || sourceSettings.inheritVelocity != 0))
			throw std::invalid_argument("Camera発生元は表面追従と速度継承に対応していません");
		if (definition.settings->trail.enabled && definition.settings->trail.maxPoints > 1024)
			throw std::invalid_argument("GPU Trailの軌跡点上限は1024です");
		const auto& emitter = definition.settings->emitter;
		if (emitter.maxParticles == 0) throw std::invalid_argument("GPUの粒子上限には1以上を指定してください");
		if (emitter.shape == ParticleEmitterShape::Circle && emitter.circle.spawnMode != ParticleEmitterSpawnMode::Random &&
			emitter.circle.velocityMode != ParticleEmitterVelocityMode::Normal)
			throw std::invalid_argument("GPUの等間隔CircleはNormal方向に対応しています");
		if (definition.phases.size() > 254) throw std::invalid_argument("GPUのPhase上限は254です");
		if (definition.settings->emitter.maxParticles > 1000000) throw std::invalid_argument("GPUの粒子上限は100万です");
		for (size_t i = 0; i < definition.phases.size(); ++i) {
			const auto& source = definition.phases[i];
			ParticleGPUPhase phase;
			phase.instructionOffset = static_cast<uint32_t>(program.instructions.size());
			phase.instructionCount = static_cast<uint32_t>(source.modules.size());
			phase.lifeEndMode = static_cast<uint32_t>(source.lifeEndMode);
			phase.lifetime = Vector4(Minimum(source.lifetime), Maximum(source.lifetime), 0, 0);
			size_t moduleIndex = 0;
			for (const auto& entry : definition.settings->phases[i].modules) {
				if (!entry.enabled) continue;
				// 実行定義と同じ順序で有効なModuleだけを変換する
				auto& module = *source.modules[moduleIndex++];
				const auto& id = entry.id;
				// 描画専用の値は寿命Curveとして評価する
				if (id == "CustomData" || id == "CustomShaderParameter") continue;
				if (id == "ShapeOverLifetime") {
					CompileParticleGPUShape(module, program);
				} else {
					program.instructions.push_back(CompileInstruction(id, module, program));
				}
			}
			phase.instructionCount = static_cast<uint32_t>(program.instructions.size()) - phase.instructionOffset;
			program.phases.push_back(phase);
		}
		CompileParticleGPUInitialShape(definition.renderSettings, program);
		if (uint64_t(program.stateCount) * definition.settings->emitter.maxParticles * sizeof(Vector4) > 256ull * 1024 * 1024)
			throw std::invalid_argument("GPUのModule状態が256MBを超えています");
	} catch (const std::exception& error) {
		// 未対応のままCPUへ切り替えず、設定を修正できる理由を残す
		program.unsupportedReason = error.what();
	}
	return program;
}
