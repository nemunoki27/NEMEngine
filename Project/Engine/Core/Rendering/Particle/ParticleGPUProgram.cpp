#include "ParticleGPUProgram.h"

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
		default: break;
		}
		return result;
	}

	bool WithinError(const Vector4& actual, const Vector4& approximation) {

		const float* a = &actual.x;
		const float* b = &approximation.x;
		for (uint32_t i = 0; i < 4; ++i) {
			if (!std::isfinite(a[i]) || std::abs(a[i] - b[i]) > 0.001f * (std::max)(1.0f, std::abs(a[i]))) return false;
		}
		return true;
	}

	void CollectCurveKeys(const nlohmann::json& value, std::vector<float>& keys, std::vector<uint32_t>& loops) {

		if (value.is_object()) {
			if (value.contains("time") && value["time"].is_number()) keys.push_back(value["time"].get<float>());
			if (value.contains("loopCount")) {
				const int32_t count = value["loopCount"].get<int32_t>();
				if (count > 4096) throw std::invalid_argument("GPUの曲線ループ上限は4096です");
				if (count > 1) loops.push_back(static_cast<uint32_t>(count));
			}
		}
		if (value.is_structured()) for (const auto& child : value) CollectCurveKeys(child, keys, loops);
	}

	std::vector<float> MakeCurveCheckPoints(IParticleModule& module) {

		std::vector<float> keys{0.0f, 1.0f};
		std::vector<uint32_t> loops{1};
		CollectCurveKeys(module.ToJson(), keys, loops);
		std::sort(loops.begin(), loops.end());
		loops.erase(std::unique(loops.begin(), loops.end()), loops.end());
		std::vector<float> result;
		// 狭い山とループ境界も検査し、等間隔の検査から漏らさない
		for (const auto count : loops) for (uint32_t cycle = 0; cycle < count; ++cycle) for (float key : keys) for (bool reverse : {false, true}) {
			if (!std::isfinite(key) || key < 0 || key > 1) continue;
			const float progress = (cycle + (reverse ? 1.0f - key : key)) / count;
			result.push_back(progress);
			result.push_back((std::max)(0.0f, std::nextafter(progress, 0.0f)));
			result.push_back((std::min)(1.0f, std::nextafter(progress, 1.0f)));
		}
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	void CompileCurve(IParticleModule& module, ParticleGPUInstruction& instruction, ParticleGPUProgram& program) {

		// CPUの評価器を使い、許容誤差を満たすまで共有テーブルを細かくする
		std::vector<ParticleGPUCurveSample> samples;
		const auto checkPoints = MakeCurveCheckPoints(module);
		for (uint32_t count = 257; count <= 4097; count = (count - 1) * 2 + 1) {
			samples.resize(count);
			for (uint32_t i = 0; i < count; ++i) samples[i] = Evaluate(module, instruction.operation, float(i) / (count - 1));
			bool valid = true;
			for (const float progress : checkPoints) {
				const float coordinate = progress * (count - 1);
				const uint32_t index = (std::min)(static_cast<uint32_t>(coordinate), count - 2);
				const float t = coordinate - index;
				const auto actual = Evaluate(module, instruction.operation, progress);
				valid &= WithinError(actual.value0, samples[index].value0 * (1 - t) + samples[index + 1].value0 * t) &&
					WithinError(actual.value1, samples[index].value1 * (1 - t) + samples[index + 1].value1 * t);
				if (!valid) break;
			}
			for (uint32_t i = 0; valid && i + 1 < count; ++i) {
				for (const float t : {0.25f, 0.5f, 0.75f}) {
					const auto actual = Evaluate(module, instruction.operation, (float(i) + t) / (count - 1));
					valid &= WithinError(actual.value0, samples[i].value0 * (1 - t) + samples[i + 1].value0 * t) &&
						WithinError(actual.value1, samples[i].value1 * (1 - t) + samples[i + 1].value1 * t);
				}
			}
			if (!valid) continue;
			instruction.curveOffset = static_cast<uint32_t>(program.curves.size());
			instruction.curveCount = count;
			program.curves.insert(program.curves.end(), samples.begin(), samples.end());
			return;
		}
		throw std::invalid_argument("GPUの曲線精度上限を超えています");
	}

	ParticleGPUInstruction CompileRotation(const ParticleRotationModule& module) {

		const auto& settings = module.GetSettings();
		if (settings.mode == ParticleRotationMode::Interpolate ||
			(settings.mode == ParticleRotationMode::Additive && settings.speedMode != ParticleRotationSpeedMode::Constant))
			throw std::invalid_argument("GPUのRotationは固定角度と一定角速度に対応しています");
		ParticleGPUInstruction result;
		result.operation = ParticleGPUOperation::Rotation;
		result.flags = settings.mode == ParticleRotationMode::Additive ? 1u : 0u;
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
			if (instruction.flags & 1u) instruction.parameter3.w = static_cast<float>(program.stateCount++);
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
		CompileCurve(module, instruction, program);
		return instruction;
	}
}

bool Engine::IsParticleGPUModuleSupported(std::string_view moduleID) {

	return FindModuleOperation(moduleID).has_value();
}

ParticleGPUProgram Engine::CompileParticleGPUProgram(const ParticleSystemDefinition& definition) {

	ParticleGPUProgram program;
	try {
		if (!definition.settings->model && definition.settings->shape != PrimitiveType::Plane &&
			(!definition.settings->trail.enabled || definition.settings->trail.drawSource))
			throw std::invalid_argument("GPUの形状はPlaneまたは共有Meshに対応しています");
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
			for (const auto& [name, parameter] : definition.settings->phases[i].materialSettings.parameters)
				if (parameter.mode != ParticleMaterialParameterMode::Constant)
					throw std::invalid_argument("GPUはマテリアル値の寿命アニメーションに対応していません: " + name);
			const auto& source = definition.phases[i];
			ParticleGPUPhase phase;
			phase.instructionOffset = static_cast<uint32_t>(program.instructions.size());
			phase.instructionCount = static_cast<uint32_t>(source.modules.size());
			phase.lifeEndMode = static_cast<uint32_t>(source.lifeEndMode);
			phase.lifetime = Vector4(Minimum(source.lifetime), Maximum(source.lifetime), 0, 0);
			for (size_t j = 0; j < source.modules.size(); ++j) {
				program.instructions.push_back(CompileInstruction(definition.settings->phases[i].modules[j].id, *source.modules[j], program));
			}
			program.phases.push_back(phase);
		}
		for (const auto& [name, parameter] : definition.settings->trail.materialSettings.parameters)
			if (parameter.mode != ParticleMaterialParameterMode::Constant)
				throw std::invalid_argument("GPUはTrailのマテリアル値の寿命アニメーションに対応していません: " + name);
		if (uint64_t(program.stateCount) * definition.settings->emitter.maxParticles * sizeof(Vector4) > 256ull * 1024 * 1024)
			throw std::invalid_argument("GPUのModule状態が256MBを超えています");
	} catch (const std::exception& error) {
		// 未対応のままCPUへ切り替えず、設定を修正できる理由を残す
		program.unsupportedReason = error.what();
	}
	return program;
}
