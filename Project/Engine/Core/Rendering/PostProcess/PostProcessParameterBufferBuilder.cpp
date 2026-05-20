#include "PostProcessParameterBufferBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

//============================================================================
//	PostProcessParameterBufferBuilder classMethods
//============================================================================

namespace {

	template<typename TValue>
	void WriteScalarArray(std::vector<uint8_t>& bytes,
		const Engine::ShaderConstantBufferVariable& variable,
		const std::array<TValue, 4>& values, uint32_t componentCount) {

		const uint32_t count = (std::min)(componentCount, variable.columns);
		for (uint32_t i = 0; i < count; ++i) {

			const size_t offset = static_cast<size_t>(variable.offset) + sizeof(TValue) * i;
			if (offset + sizeof(TValue) <= bytes.size()) {
				std::memcpy(bytes.data() + offset, &values[i], sizeof(TValue));
			}
		}
	}

	std::array<float, 4> ToFloatArray(const Engine::MaterialParameterValue& parameter, uint32_t& outCount) {

		outCount = 1;
		return std::visit([&](const auto& value) -> std::array<float, 4> {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				return { value, 0.0f, 0.0f, 0.0f };
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				outCount = 2;
				return { value.x, value.y, 0.0f, 0.0f };
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				outCount = 3;
				return { value.x, value.y, value.z, 0.0f };
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				outCount = 4;
				return { value.x, value.y, value.z, value.w };
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				outCount = 4;
				return { value.r, value.g, value.b, value.a };
			} else if constexpr (std::is_same_v<ValueType, int32_t> || std::is_same_v<ValueType, uint32_t>) {
				return { static_cast<float>(value), 0.0f, 0.0f, 0.0f };
			} else if constexpr (std::is_same_v<ValueType, bool>) {
				return { value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
			} else {
				return {};
			}
			}, parameter.value);
	}

	std::array<uint32_t, 4> ToUIntArray(const Engine::MaterialParameterValue& parameter, uint32_t& outCount) {

		outCount = 1;
		return std::visit([&](const auto& value) -> std::array<uint32_t, 4> {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, uint32_t>) {
				return { value, 0u, 0u, 0u };
			} else if constexpr (std::is_same_v<ValueType, int32_t>) {
				return { static_cast<uint32_t>(value), 0u, 0u, 0u };
			} else if constexpr (std::is_same_v<ValueType, bool>) {
				return { value ? 1u : 0u, 0u, 0u, 0u };
			} else if constexpr (std::is_same_v<ValueType, float>) {
				return { static_cast<uint32_t>(value), 0u, 0u, 0u };
			} else {
				return {};
			}
			}, parameter.value);
	}

	std::array<int32_t, 4> ToIntArray(const Engine::MaterialParameterValue& parameter, uint32_t& outCount) {

		outCount = 1;
		return std::visit([&](const auto& value) -> std::array<int32_t, 4> {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, int32_t>) {
				return { value, 0, 0, 0 };
			} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
				return { static_cast<int32_t>(value), 0, 0, 0 };
			} else if constexpr (std::is_same_v<ValueType, bool>) {
				return { value ? 1 : 0, 0, 0, 0 };
			} else if constexpr (std::is_same_v<ValueType, float>) {
				return { static_cast<int32_t>(value), 0, 0, 0 };
			} else {
				return {};
			}
			}, parameter.value);
	}

	void WriteParameterValue(std::vector<uint8_t>& bytes,
		const Engine::ShaderConstantBufferVariable& variable,
		const Engine::MaterialParameterValue& parameter) {

		if (variable.offset >= bytes.size()) {
			return;
		}

		uint32_t count = 1;
		switch (variable.valueType) {
		case D3D_SVT_FLOAT:
			WriteScalarArray(bytes, variable, ToFloatArray(parameter, count), count);
			break;
		case D3D_SVT_INT:
			WriteScalarArray(bytes, variable, ToIntArray(parameter, count), count);
			break;
		case D3D_SVT_UINT:
		case D3D_SVT_BOOL:
			WriteScalarArray(bytes, variable, ToUIntArray(parameter, count), count);
			break;
		default:
			Engine::Logger::Output(Engine::LogType::Engine, "[PostProcess] unsupported parameter type. name=" + variable.name);
			break;
		}
	}
}

std::vector<uint8_t> Engine::PostProcessParameterBufferBuilder::Build(
	const MaterialAsset& material, const PostProcessParameterLayout& layout) {

	std::vector<uint8_t> bytes((std::max)(layout.GetSizeInBytes(), 16u), 0);
	for (const ShaderConstantBufferVariable& variable : layout.GetVariables()) {

		auto found = material.parameters.find(variable.name);
		if (found == material.parameters.end()) {
			continue;
		}
		// Reflectionのoffsetへ直接詰めることで、HLSL側のパッキングに追従する。
		WriteParameterValue(bytes, variable, found->second);
	}
	return bytes;
}
