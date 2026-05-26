#include "PostProcessParameterLayout.h"

// c++
#include <algorithm>

//============================================================================
//	PostProcessParameterLayout classMethods
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

void Engine::PostProcessParameterLayout::Build(const ShaderReflectionInfo& reflection) {

	sizeInBytes_ = 0;
	bindPoint_ = 1;
	space_ = 0;
	variables_.clear();

	for (const ShaderConstantBufferInfo& buffer : reflection.constantBuffers) {
		if (buffer.name != "PostProcessParameters") {
			continue;
		}

		sizeInBytes_ = buffer.size;
		bindPoint_ = buffer.bindPoint;
		space_ = buffer.space;
		variables_ = buffer.variables;

		for (const ShaderConstantBufferVariable& variable : variables_) {
			const uint32_t declaredEnd = variable.offset + GetDeclaredVariableByteSize(variable);
			sizeInBytes_ = (std::max)(sizeInBytes_, declaredEnd);
		}
		sizeInBytes_ = AlignConstantBufferSize(sizeInBytes_);
		return;
	}
}
