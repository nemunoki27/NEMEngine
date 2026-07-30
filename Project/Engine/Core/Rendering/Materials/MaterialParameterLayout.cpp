#include "MaterialParameterLayout.h"

// c++
#include <algorithm>

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

		for (const ShaderConstantBufferVariable& variable : variables_) {
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
