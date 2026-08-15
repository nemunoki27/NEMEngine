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

const Engine::ShaderStructuredBufferInfo* Engine::FindStructuredBuffer(
	const ShaderReflectionInfo& reflection, std::string_view name) {

	for (const ShaderStructuredBufferInfo& buffer : reflection.structuredBuffers) {
		if (buffer.name == name) {
			return &buffer;
		}
	}
	return nullptr;
}

void Engine::MergeShaderReflection(
	ShaderReflectionInfo& target,
	const ShaderReflectionInfo& source) {

	for (const ShaderResourceBinding& resource : source.resources) {
		auto found = std::find_if(target.resources.begin(), target.resources.end(),
			[&](const ShaderResourceBinding& current) {
				return current.kind == resource.kind &&
					current.bindPoint == resource.bindPoint &&
					current.space == resource.space;
			});
		if (found != target.resources.end()) {
			found->stageMask |= resource.stageMask;
			found->bindCount = (std::max)(found->bindCount, resource.bindCount);
		} else {
			target.resources.emplace_back(resource);
		}
	}
	for (const ShaderConstantBufferInfo& buffer : source.constantBuffers) {
		const auto found = std::find_if(target.constantBuffers.begin(),
			target.constantBuffers.end(), [&](const ShaderConstantBufferInfo& current) {
				return current.name == buffer.name &&
					current.bindPoint == buffer.bindPoint &&
					current.space == buffer.space;
			});
		if (found == target.constantBuffers.end()) {
			target.constantBuffers.emplace_back(buffer);
		}
	}
	for (const ShaderStructuredBufferInfo& buffer : source.structuredBuffers) {
		const auto found = std::find_if(target.structuredBuffers.begin(),
			target.structuredBuffers.end(), [&](const ShaderStructuredBufferInfo& current) {
				return current.name == buffer.name &&
					current.bindPoint == buffer.bindPoint &&
					current.space == buffer.space;
			});
		if (found == target.structuredBuffers.end()) {
			target.structuredBuffers.emplace_back(buffer);
		}
	}
	target.requiresFlags |= source.requiresFlags;
}
