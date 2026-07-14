#include "ParticleRenderDataUtility.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cstring>

//============================================================================
//	ParticleRenderDataUtility functions
//============================================================================
Engine::ParticleCustomParameterLayout Engine::BuildParticleCustomParameterLayout(
	const ShaderReflectionInfo& reflection) {

	ParticleCustomParameterLayout layout{};
	const ShaderStructuredBufferInfo* buffer = FindStructuredBuffer(reflection, "gParticleCustomParameters");
	if (!buffer || buffer->stride == 0) {
		return layout;
	}
	layout.stride = buffer->stride;
	for (const ShaderConstantBufferVariable& variable : buffer->variables) {
		if (variable.valueType == D3D_SVT_FLOAT && variable.offset < layout.stride) {
			layout.variables.emplace_back(variable);
		}
	}
	return layout;
}

void Engine::WriteParticleCustomParameter(std::vector<uint8_t>& data,
	const ShaderConstantBufferVariable& variable, const Vector4& value) {

	const uint32_t componentCount = GetVariableComponentCount(variable);
	const uint32_t writeSize = (std::min)(componentCount * static_cast<uint32_t>(sizeof(float)), variable.size);
	if (writeSize == 0 || variable.offset + writeSize > data.size()) {
		return;
	}
	const float values[4] = { value.x, value.y, value.z, value.w };
	std::memcpy(data.data() + variable.offset, values, writeSize);
}
