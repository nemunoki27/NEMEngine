#include "ParticleRenderDataUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>

//============================================================================
//	ParticleRenderDataUtility functions
//============================================================================
Engine::ParticleCustomParameterLayout Engine::BuildParticleCustomParameterLayout(
	const ShaderReflectionInfo& reflection, const MaterialParameterSet* defaults,
	const MaterialParameterBufferBuilder::TextureResolver& resolveTexture) {

	ParticleCustomParameterLayout layout{};
	const ShaderStructuredBufferInfo* buffer = FindStructuredBuffer(reflection, "gParticleCustomParameters");
	if (!buffer || buffer->stride == 0) {
		return layout;
	}
	layout.stride = buffer->stride;
	for (const ShaderConstantBufferVariable& variable : buffer->variables) {
		const bool supported = variable.valueType == D3D_SVT_FLOAT ||
			variable.valueType == D3D_SVT_INT || variable.valueType == D3D_SVT_UINT ||
			variable.valueType == D3D_SVT_BOOL;
		if (supported && variable.offset < layout.stride) {
			layout.variables.emplace_back(variable);
		}
	}
	layout.defaultData.resize((std::max)(layout.stride, 16u), 0);
	if (defaults) {
		MaterialParameterLayout parameterLayout{};
		parameterLayout.Build(reflection, "gParticleCustomParameters");
		MaterialParameterBufferBuilder::BuildElementInto(
			layout.defaultData, *defaults, {}, parameterLayout,
			resolveTexture);
	}
	layout.defaultData.resize(layout.stride);
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
	if (variable.valueType == D3D_SVT_FLOAT) {
		std::memcpy(data.data() + variable.offset, values, writeSize);
		return;
	}
	if (variable.valueType == D3D_SVT_INT) {
		const int32_t converted[4] = {
			static_cast<int32_t>(value.x), static_cast<int32_t>(value.y),
			static_cast<int32_t>(value.z), static_cast<int32_t>(value.w),
		};
		std::memcpy(data.data() + variable.offset, converted, writeSize);
		return;
	}
	const std::array<uint32_t, 4> converted = variable.valueType == D3D_SVT_BOOL ?
		std::array<uint32_t, 4>{
			value.x != 0.0f ? 1u : 0u, value.y != 0.0f ? 1u : 0u,
			value.z != 0.0f ? 1u : 0u, value.w != 0.0f ? 1u : 0u,
		} :
		std::array<uint32_t, 4>{
			static_cast<uint32_t>((std::max)(value.x, 0.0f)),
			static_cast<uint32_t>((std::max)(value.y, 0.0f)),
			static_cast<uint32_t>((std::max)(value.z, 0.0f)),
			static_cast<uint32_t>((std::max)(value.w, 0.0f)),
		};
	std::memcpy(data.data() + variable.offset, converted.data(), writeSize);
}

const Engine::ParticleGroupRuntimeState* Engine::ResolveParticleRenderGroup(
	const RenderItem& item, const ParticleRenderPayload& payload) {

	if (!item.world) {
		return nullptr;
	}
	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(*item.world, item.entity);
	if (!runtime) {
		return nullptr;
	}
	const std::vector<ParticleGroupRuntimeState>& groups =
		runtime->effect.runtimeGroups;
	if (payload.groupIndex < groups.size() &&
		groups[payload.groupIndex].groupID == payload.groupID) {

		return &groups[payload.groupIndex];
	}
	const auto it = std::find_if(groups.begin(), groups.end(),
		[&](const ParticleGroupRuntimeState& group) {
			return group.groupID == payload.groupID;
		});
	return it != groups.end() ? &*it : nullptr;
}
