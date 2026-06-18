#include "ShaderReflection.h"

// c++
#include <algorithm>

using namespace Engine;

//============================================================================
//	ShaderReflection classMethods
//============================================================================
ShaderStage Engine::operator|(ShaderStage a, ShaderStage b) {

	using T = std::underlying_type_t<ShaderStage>;
	return static_cast<ShaderStage>(static_cast<T>(a) | static_cast<T>(b));
}

ShaderStage& Engine::operator|=(ShaderStage& a, ShaderStage b) {

	a = a | b;
	return a;
}

const Engine::ShaderConstantBufferInfo* Engine::FindConstantBuffer(
	const ShaderReflectionInfo& reflection, std::string_view name) {

	for (const ShaderConstantBufferInfo& buffer : reflection.constantBuffers) {
		if (buffer.name == name) {
			return &buffer;
		}
	}
	return nullptr;
}

uint32_t Engine::GetVariableComponentCount(const ShaderConstantBufferVariable& variable) {

	uint32_t count = (std::max)(1u, variable.declaredComponentCount);
	if (variable.columns > 0) {
		count = (std::max)(count, variable.columns);
	}
	if (variable.rows > 0 && variable.columns > 0) {
		count = (std::max)(count, variable.rows * variable.columns);
	}
	if (count <= 1 && variable.size > sizeof(float)) {
		count = static_cast<uint32_t>(variable.size / sizeof(float));
	}
	return (std::min)(count, 4u);
}