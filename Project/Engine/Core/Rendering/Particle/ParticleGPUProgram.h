#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Effect/ParticleDefinition.h>

// c++
#include <string_view>

namespace Engine {

	// GPUが実行するModule命令
	enum class ParticleGPUOperation : uint32_t {

		Color, Size, Scale, Emissive, AlphaReference, UV, Gravity, Rotation, LookToVelocity, Flipbook,
	};

	struct ParticleGPUCurveSample {

		Vector4 value0 = Vector4(0, 0, 0, 0);
		Vector4 value1 = Vector4(0, 0, 0, 0);
	};
	static_assert(sizeof(ParticleGPUCurveSample) == 32);

	struct ParticleGPUInstruction {

		ParticleGPUOperation operation{};
		uint32_t curveOffset = 0;
		uint32_t curveCount = 0;
		uint32_t flags = 0;
		Vector4 parameter0 = Vector4(0, 0, 0, 0);
		Vector4 parameter1 = Vector4(0, 0, 0, 0);
		Vector4 parameter2 = Vector4(0, 0, 0, 0);
		Vector4 parameter3 = Vector4(0, 0, 0, 0);
	};
	static_assert(sizeof(ParticleGPUInstruction) == 80);

	struct ParticleGPUPhase {

		uint32_t instructionOffset = 0;
		uint32_t instructionCount = 0;
		uint32_t lifeEndMode = 0;
		uint32_t reserved = 0;
		Vector4 lifetime = Vector4(1, 1, 0, 0);
	};
	static_assert(sizeof(ParticleGPUPhase) == 32);

	// 全粒子で共有する変更不可のGPU実行定義
	struct ParticleGPUProgram {

		std::vector<ParticleGPUInstruction> instructions;
		std::vector<ParticleGPUCurveSample> curves;
		std::vector<ParticleGPUPhase> phases;
		uint32_t stateCount = 0;
		std::string unsupportedReason;
	};

	// Moduleを命令へ変換し、未対応の入力は理由を返す
	ParticleGPUProgram CompileParticleGPUProgram(const ParticleSystemDefinition& definition);
	// GPUの命令へ変換できるModuleか確認する
	bool IsParticleGPUModuleSupported(std::string_view moduleID);
}
