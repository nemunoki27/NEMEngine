#include "ParticleGPUMaterialCurves.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUCurveCompiler.h>
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

ParticleGPUMaterialCurves Engine::BuildParticleGPUMaterialCurves(const ParticleCustomParameterLayout& layout,
	const ParticlePhaseMaterialSettings& material, const ParticleCustomDataSettings& customData) {

	ParticleGPUProgram program;
	ParticleGPUMaterialCurves result;
	result.headers.resize(layout.stride / sizeof(float) + 1);
	const auto compile = [&](uint32_t index, bool constant, const nlohmann::json& parameters,
		const ParticleGPUCurveEvaluator& evaluate) {
		if (constant) {
			const auto value = evaluate(0.0f);
			for (uint32_t component = 0; component < 4; ++component) {
				if (!std::isfinite((&value.value0.x)[component]) || !std::isfinite((&value.value1.x)[component]))
					throw std::invalid_argument("GPUの任意数値には有限の値を指定してください");
			}
			result.headers[index] = {static_cast<uint32_t>(program.curves.size()), 1};
			program.curves.push_back(value);
			return;
		}
		ParticleGPUInstruction instruction;
		CompileParticleGPUCurve(parameters, evaluate, instruction, program);
		result.headers[index] = {instruction.curveOffset, instruction.curveCount};
	};
	if (customData.enabled) {
		nlohmann::json parameters;
		to_json(parameters, customData);
		const bool constant = customData.customData1.mode == ParticleMaterialParameterMode::Constant &&
			customData.customData2.mode == ParticleMaterialParameterMode::Constant;
		compile(0, constant, parameters, [&](float t) {
			return ParticleGPUCurveSample{EvaluateParticleMaterialParameter(customData.customData1, t),
				EvaluateParticleMaterialParameter(customData.customData2, t)};
		});
	}
	for (const auto& variable : layout.variables) {
		const auto found = material.parameters.find(variable.name);
		if (found == material.parameters.end() || variable.isTexture) continue;
		nlohmann::json parameters;
		to_json(parameters, found->second);
		compile(variable.offset / sizeof(float) + 1, found->second.mode == ParticleMaterialParameterMode::Constant,
			parameters, [&](float t) {
			return ParticleGPUCurveSample{EvaluateParticleMaterialParameter(found->second, t)};
		});
	}
	result.samples = std::move(program.curves);
	if (result.samples.empty()) result.samples.emplace_back();
	return result;
}

const ParticleGPUMaterialCurves& ParticleGPUMaterialCurveCache::Get(const ParticleRenderSettings& settings,
	AssetID materialID, uint32_t phase, uint64_t shaderRevision, const ParticleCustomParameterLayout& layout,
	const ParticlePhaseMaterialSettings& material, const ParticleCustomDataSettings& customData) {

	if (!settings.authoringOwner) {
		transient_ = BuildParticleGPUMaterialCurves(layout, material, customData);
		return transient_;
	}
	for (const auto& entry : entries_) {
		if (entry.owner.lock() == settings.authoringOwner && entry.materialID == materialID && entry.phase == phase &&
			entry.shaderRevision == shaderRevision) return entry.curves;
	}
	std::erase_if(entries_, [&](const auto& entry) {
		return entry.owner.lock() == settings.authoringOwner && entry.materialID == materialID && entry.phase == phase;
	});
	entries_.push_back({settings.authoringOwner, materialID, phase, shaderRevision,
		BuildParticleGPUMaterialCurves(layout, material, customData)});
	return entries_.back().curves;
}

void ParticleGPUMaterialCurveCache::BeginFrame() {

	std::erase_if(entries_, [](const auto& entry) { return entry.owner.expired(); });
}
