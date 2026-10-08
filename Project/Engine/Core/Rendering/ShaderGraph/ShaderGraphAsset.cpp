#include "ShaderGraphAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

bool Engine::IsShaderGraph3DTarget(ShaderGraphTarget target) {

	return target == ShaderGraphTarget::Mesh || target == ShaderGraphTarget::Primitive3D;
}

bool Engine::SupportsShaderGraphVertexOutput(ShaderGraphTarget target) {

	return target == ShaderGraphTarget::Mesh || target == ShaderGraphTarget::Primitive3D ||
		   target == ShaderGraphTarget::Primitive2D;
}

std::string_view Engine::GetShaderGraphNodeName(ShaderGraphNodeKind kind) {

	const ShaderGraphNodeDescriptor* descriptor = ShaderGraphNodeRegistry::Find(kind);
	return descriptor ? descriptor->name : "Unknown";
}

uint32_t Engine::GetShaderGraphInputCount(ShaderGraphNodeKind kind) {

	const ShaderGraphNodeDescriptor* descriptor = ShaderGraphNodeRegistry::Find(kind);
	return descriptor ? static_cast<uint32_t>(descriptor->inputs.size()) : 0u;
}

uint32_t Engine::GetShaderGraphOutputCount(ShaderGraphNodeKind kind) {

	const ShaderGraphNodeDescriptor* descriptor = ShaderGraphNodeRegistry::Find(kind);
	return descriptor ? static_cast<uint32_t>(descriptor->outputs.size()) : 0u;
}

std::string_view Engine::GetShaderGraphInputName(ShaderGraphNodeKind kind, uint32_t slot) {

	const ShaderGraphNodeDescriptor* descriptor = ShaderGraphNodeRegistry::Find(kind);
	return descriptor && slot < descriptor->inputs.size() ? descriptor->inputs[slot].name : std::string_view{};
}

std::string_view Engine::GetShaderGraphOutputName(ShaderGraphNodeKind kind, uint32_t slot) {

	const ShaderGraphNodeDescriptor* descriptor = ShaderGraphNodeRegistry::Find(kind);
	return descriptor && slot < descriptor->outputs.size() ? descriptor->outputs[slot].name : std::string_view{};
}

uint32_t Engine::GetShaderGraphInputCount(const ShaderGraphNode& node) {

	return node.inputPorts.empty() ? GetShaderGraphInputCount(node.kind) : static_cast<uint32_t>(node.inputPorts.size());
}

uint32_t Engine::GetShaderGraphOutputCount(const ShaderGraphNode& node) {

	return node.outputPorts.empty() ? GetShaderGraphOutputCount(node.kind) : static_cast<uint32_t>(node.outputPorts.size());
}

std::string_view Engine::GetShaderGraphInputName(const ShaderGraphNode& node, uint32_t slot) {

	if (!node.inputPorts.empty()) {
		return slot < node.inputPorts.size() ? node.inputPorts[slot].name : std::string_view{};
	}
	return GetShaderGraphInputName(node.kind, slot);
}

std::string_view Engine::GetShaderGraphOutputName(const ShaderGraphNode& node, uint32_t slot) {

	if (!node.outputPorts.empty()) {
		return slot < node.outputPorts.size() ? node.outputPorts[slot].name : std::string_view{};
	}
	return GetShaderGraphOutputName(node.kind, slot);
}
