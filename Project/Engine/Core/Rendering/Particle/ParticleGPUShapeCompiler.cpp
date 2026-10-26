#include "ParticleGPUShapeCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUCurveCompiler.h"
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeDataUtility.h>

using namespace Engine;

namespace {

	// CPUの形状評価結果を2属性ずつ曲線へまとめる
	ParticleGPUCurveSample PackShape(const ParticleShapeData& shape, uint32_t part) {

		ParticleGPUCurveSample sample;
		if (part == 0) {
			sample.value0 = shape.params0;
			sample.value1 = shape.params1;
		} else if (part == 1) {
			sample.value0 = Vector4(shape.topColor.r, shape.topColor.g, shape.topColor.b, shape.topColor.a);
			sample.value1 = Vector4(shape.centerColor.r, shape.centerColor.g, shape.centerColor.b, shape.centerColor.a);
		} else {
			sample.value0 = Vector4(shape.bottomColor.r, shape.bottomColor.g, shape.bottomColor.b, shape.bottomColor.a);
		}
		return sample;
	}
}

void Engine::CompileParticleGPUShape(IParticleModule& module, ParticleGPUProgram& program) {

	// Phaseが変わっても同じ形状状態を引き継ぐ
	if (program.shapeStateSlot == UINT32_MAX) {
		program.shapeStateSlot = program.stateCount;
		program.stateCount += 5;
	}
	for (uint32_t part = 0; part < 3; ++part) {
		ParticleGPUInstruction instruction;
		instruction.operation = ParticleGPUOperation::Shape;
		instruction.flags = part;
		CompileParticleGPUCurve(module.ToJson(), [&](float progress) {
			Particle particle;
			particle.age = progress;
			module.OnUpdate(particle, 0.0f);
			return PackShape(particle.shapeData, part);
		}, instruction, program);
		program.instructions.push_back(instruction);
	}
}

void Engine::CompileParticleGPUInitialShape(const ParticleRenderSettings& settings, ParticleGPUProgram& program) {

	if (program.shapeStateSlot == UINT32_MAX) return;
	program.initialShapeOffset = static_cast<uint32_t>(program.curves.size());
	const ParticleShapeData initial = MakeParticleShapeData(settings);
	for (uint32_t part = 0; part < 3; ++part) program.curves.push_back(PackShape(initial, part));
}
