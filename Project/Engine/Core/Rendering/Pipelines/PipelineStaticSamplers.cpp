#include "PipelineState.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

using namespace Engine;

namespace {

	bool IsSameStaticSamplerSlot(const D3D12_STATIC_SAMPLER_DESC& sampler, UINT shaderRegister, UINT registerSpace) {

		return sampler.ShaderRegister == shaderRegister && sampler.RegisterSpace == registerSpace;
	}

	D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerDesc(
		const PipelineStaticSamplerSettings& settings, UINT shaderRegister, UINT registerSpace) {

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = settings.filter;
		sampler.AddressU = settings.addressU;
		sampler.AddressV = settings.addressV;
		sampler.AddressW = settings.addressW;
		sampler.MipLODBias = settings.mipLODBias;
		sampler.MaxAnisotropy = (std::clamp)(settings.maxAnisotropy, 1u, 16u);
		sampler.ComparisonFunc = settings.comparisonFunc;
		sampler.BorderColor = settings.borderColor;
		sampler.MinLOD = settings.minLOD;
		sampler.MaxLOD = settings.maxLOD;
		sampler.ShaderRegister = shaderRegister;
		sampler.RegisterSpace = registerSpace;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		return sampler;
	}

	void ReplaceStaticSampler(std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers,
		const D3D12_STATIC_SAMPLER_DESC& sampler) {

		auto found = std::find_if(staticSamplers.begin(), staticSamplers.end(), [&](const D3D12_STATIC_SAMPLER_DESC& existing) {
			return IsSameStaticSamplerSlot(existing, sampler.ShaderRegister, sampler.RegisterSpace);
			});
		if (found != staticSamplers.end()) {
			*found = sampler;
			return;
		}
		staticSamplers.emplace_back(sampler);
	}

	bool HasStaticSamplerSlot(const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers,
		UINT shaderRegister, UINT registerSpace) {

		return std::any_of(staticSamplers.begin(), staticSamplers.end(), [&](const D3D12_STATIC_SAMPLER_DESC& sampler) {
			return IsSameStaticSamplerSlot(sampler, shaderRegister, registerSpace);
			});
	}

	std::vector<D3D12_STATIC_SAMPLER_DESC> BuildComputeStaticSamplers(
		const ShaderReflectionInfo& reflection, const std::vector<D3D12_STATIC_SAMPLER_DESC>& baseSamplers,
		const PipelineStaticSamplerOverrideSet& overrides) {

		std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers = baseSamplers;
		for (const ShaderResourceBinding& binding : reflection.resources) {

			if (binding.kind != ShaderBindingKind::Sampler) {
				continue;
			}

			const auto overrideIt = overrides.byName.find(binding.name);
			const bool hasOverride = overrideIt != overrides.byName.end();
			if (!hasOverride && !overrides.fillMissingSamplers) {
				continue;
			}

			const PipelineStaticSamplerSettings settings =
				hasOverride ? overrideIt->second : PipelineStaticSamplerSettings{};
			const UINT count = (std::max)(1u, binding.bindCount);
			for (UINT i = 0; i < count; ++i) {

				const UINT shaderRegister = binding.bindPoint + i;
				if (!hasOverride && HasStaticSamplerSlot(staticSamplers, shaderRegister, binding.space)) {
					continue;
				}
				ReplaceStaticSampler(staticSamplers, MakeStaticSamplerDesc(settings, shaderRegister, binding.space));
			}
		}
		return staticSamplers;
	}
}

std::vector<D3D12_STATIC_SAMPLER_DESC>
Engine::BuildPipelineStaticSamplers(
	const ShaderReflectionInfo& reflection,
	const std::vector<D3D12_STATIC_SAMPLER_DESC>& baseSamplers,
	const PipelineStaticSamplerOverrideSet& overrides) {

	return BuildComputeStaticSamplers(reflection, baseSamplers, overrides);
}

uint64_t Engine::HashPipelineStaticSamplerOverrides(
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	if (!samplerOverrides) {
		return 0;
	}

	uint64_t hash = 1469598103934665603ull;
	const auto mix = [&hash](uint64_t value) {

		hash ^= value;
		hash *= 1099511628211ull;
	};
	mix(samplerOverrides->fillMissingSamplers ? 1ull : 0ull);

	std::vector<std::string> names{};
	names.reserve(samplerOverrides->byName.size());
	for (const auto& [name, settings] : samplerOverrides->byName) {

		names.emplace_back(name);
	}
	std::sort(names.begin(), names.end());
	for (const std::string& name : names) {

		const PipelineStaticSamplerSettings& settings =
			samplerOverrides->byName.at(name);
		mix(std::hash<std::string>{}(name));
		mix(static_cast<uint64_t>(settings.filter));
		mix(static_cast<uint64_t>(settings.addressU));
		mix(static_cast<uint64_t>(settings.addressV));
		mix(static_cast<uint64_t>(settings.addressW));
		mix(static_cast<uint64_t>(settings.borderColor));
		mix(static_cast<uint64_t>(settings.comparisonFunc));
		mix(static_cast<uint64_t>(settings.maxAnisotropy));
		mix(std::hash<float>{}(settings.mipLODBias));
		mix(std::hash<float>{}(settings.minLOD));
		mix(std::hash<float>{}(settings.maxLOD));
	}
	return hash;
}
