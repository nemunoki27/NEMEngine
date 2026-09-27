#include "MaterialParameterLayout.h"
#include "MaterialParameterLookup.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <functional>

//============================================================================
//	MaterialParameterLayout classMethods
//============================================================================
namespace {

	uint32_t AlignConstantBufferSize(uint32_t size) {

		constexpr uint32_t kAlignment = 16;
		return (size + kAlignment - 1u) & ~(kAlignment - 1u);
	}

	uint32_t GetDeclaredVariableByteSize(const Engine::ShaderConstantBufferVariable& variable) {

		if (variable.declaredByteSize > 0) {
			return variable.declaredByteSize;
		}
		if (variable.size > 0) {
			return variable.size;
		}
		return sizeof(float);
	}
}

void Engine::MaterialParameterLayout::Build(const ShaderReflectionInfo& reflection,
	const std::string& cbufferName) {

	sizeInBytes_ = 0;
	bindPoint_ = 1;
	space_ = 0;
	variables_.clear();

	const ShaderConstantBufferInfo* buffer = FindConstantBuffer(reflection, cbufferName);
	if (buffer) {

		sizeInBytes_ = buffer->size;
		bindPoint_ = buffer->bindPoint;
		space_ = buffer->space;
		variables_ = buffer->variables;

		for (ShaderConstantBufferVariable& variable : variables_) {
			variable.isTexture = MaterialParameterLookup::IsTexture(variable, reflection);
			const uint32_t declaredEnd = variable.offset + GetDeclaredVariableByteSize(variable);
			sizeInBytes_ = (std::max)(sizeInBytes_, declaredEnd);
		}
		std::sort(variables_.begin(), variables_.end(),
			[](const ShaderConstantBufferVariable& lhs,
				const ShaderConstantBufferVariable& rhs) {
				return lhs.parameterID.value < rhs.parameterID.value;
			});
		sizeInBytes_ = AlignConstantBufferSize(sizeInBytes_);
		return;
	}

	const ShaderStructuredBufferInfo* structuredBuffer = FindStructuredBuffer(reflection, cbufferName);
	if (structuredBuffer) {

		sizeInBytes_ = structuredBuffer->stride;
		bindPoint_ = structuredBuffer->bindPoint;
		space_ = structuredBuffer->space;
		variables_ = structuredBuffer->variables;
		for (auto& variable : variables_) variable.isTexture = MaterialParameterLookup::IsTexture(variable, reflection);
		std::sort(variables_.begin(), variables_.end(),
			[](const ShaderConstantBufferVariable& lhs,
				const ShaderConstantBufferVariable& rhs) {
				return lhs.parameterID.value < rhs.parameterID.value;
			});
	}
}

const Engine::ShaderConstantBufferVariable*
Engine::MaterialParameterLayout::Find(MaterialParameterID id) const {

	const auto position = std::lower_bound(variables_.begin(), variables_.end(), id.value,
		[](const ShaderConstantBufferVariable& variable, uint64_t target) {
			return variable.parameterID.value < target;
		});
	return position != variables_.end() && position->parameterID == id ?
		&*position : nullptr;
}

uint64_t Engine::MaterialParameterLayout::GetContentHash() const {

	uint64_t hash = 1469598103934665603ull;
	Algorithm::HashCombine(hash, sizeInBytes_);
	Algorithm::HashCombine(hash, bindPoint_);
	Algorithm::HashCombine(hash, space_);
	for (const auto& variable : variables_) {
		// 同じ配置でもIDやTexture指定が変われば再転送する
		Algorithm::HashCombine(hash, std::hash<std::string>{}(variable.name));
		Algorithm::HashCombine(hash, variable.parameterID.value);
		Algorithm::HashCombine(hash, static_cast<uint64_t>(variable.semantic));
		Algorithm::HashCombine(hash, variable.offset);
		Algorithm::HashCombine(hash, variable.size);
		Algorithm::HashCombine(hash, static_cast<uint64_t>(variable.valueClass));
		Algorithm::HashCombine(hash, static_cast<uint64_t>(variable.valueType));
		Algorithm::HashCombine(hash, variable.rows);
		Algorithm::HashCombine(hash, variable.columns);
		Algorithm::HashCombine(hash, variable.elements);
		Algorithm::HashCombine(hash, variable.declaredComponentCount);
		Algorithm::HashCombine(hash, variable.declaredByteSize);
		Algorithm::HashCombine(hash, variable.used);
		Algorithm::HashCombine(hash, variable.isColor);
		Algorithm::HashCombine(hash, variable.isTexture);
	}
	return hash;
}
