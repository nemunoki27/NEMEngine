#include "MaterialParameterBufferBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <variant>

//============================================================================
//	MaterialParameterBufferBuilder classMethods
//============================================================================
namespace {

	template<typename TValue>
	uint32_t GetMaxWritableComponentCount(const Engine::ShaderConstantBufferVariable& variable,
		uint32_t layoutSizeInBytes) {

		if (variable.offset >= layoutSizeInBytes) {
			return 0;
		}

		const uint32_t remainingBytes = layoutSizeInBytes - variable.offset;
		uint32_t declaredBytes = variable.declaredByteSize;
		if (declaredBytes == 0) {
			declaredBytes = variable.size;
		}
		if (declaredBytes == 0) {
			declaredBytes = static_cast<uint32_t>(sizeof(TValue));
		}

		const uint32_t writableBytes = (std::min)(remainingBytes, declaredBytes);
		return (std::min)(static_cast<uint32_t>(writableBytes / sizeof(TValue)), 4u);
	}

	uint32_t GetDeclaredComponentCount(const Engine::ShaderConstantBufferVariable& variable) {

		uint32_t count = variable.declaredComponentCount;
		if (count == 0 && variable.declaredByteSize > 0) {
			count = variable.declaredByteSize / sizeof(float);
		}
		if (count == 0 && variable.size > 0) {
			count = variable.size / sizeof(float);
		}
		return std::clamp<uint32_t>(count, 1u, 4u);
	}


	const char* GetParameterValueTypeName(const Engine::MaterialParameterValue& parameter) {

		return std::visit([](const auto& value) -> const char* {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				return "float";
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				return "Vector2";
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				return "Vector3";
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				return "Vector4";
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				return "Color4";
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
				return "AssetID";
			} else if constexpr (std::is_same_v<ValueType, int32_t>) {
				return "int32";
			} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
				return "uint32";
			} else if constexpr (std::is_same_v<ValueType, bool>) {
				return "bool";
			} else {
				return "unknown";
			}
			}, parameter.value);
	}

	bool IsParameterDebugLogEnabled() {

#if defined(_DEBUG)
		static const bool enabled = []() {
#if defined(_MSC_VER)
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, "NEM_MATERIAL_PARAM_DEBUG") != 0 || value == nullptr) {
				return false;
			}
			const bool result = length > 0 && value[0] != '\0' && value[0] != '0';
			std::free(value);
			return result;
#else
			const char* value = std::getenv("NEM_MATERIAL_PARAM_DEBUG");
			return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
		}();
		return enabled;
#else
		return false;
#endif
	}

	template<typename TValue>
	uint32_t WriteScalarArray(std::vector<uint8_t>& bytes,
		const Engine::ShaderConstantBufferVariable& variable,
		const std::array<TValue, 4>& values, uint32_t componentCount,
		uint32_t layoutSizeInBytes) {

		const uint32_t writableComponentCount =
			GetMaxWritableComponentCount<TValue>(variable, layoutSizeInBytes);
		const uint32_t declaredComponentCount = GetDeclaredComponentCount(variable);
		const uint32_t count = (std::min)({ componentCount, writableComponentCount, declaredComponentCount, 4u });
		for (uint32_t i = 0; i < count; ++i) {

			const size_t offset = static_cast<size_t>(variable.offset) + sizeof(TValue) * i;
			if (offset + sizeof(TValue) <= bytes.size()) {
				std::memcpy(bytes.data() + offset, &values[i], sizeof(TValue));
			}
		}
		return count;
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

	float ExtractFloatComponent(const Engine::MaterialParameterValue& parameter, uint32_t index) {

		uint32_t count = 0;
		const std::array<float, 4> values = ToFloatArray(parameter, count);
		return index < values.size() ? values[index] : 0.0f;
	}

	Engine::MaterialParameterValue NormalizeParameterValueForVariable(
		const Engine::ShaderConstantBufferVariable& variable,
		const Engine::MaterialParameterValue& src) {

		if (variable.valueType != D3D_SVT_FLOAT) {
			return src;
		}

		const uint32_t componentCount = GetDeclaredComponentCount(variable);
		if (componentCount <= 1) {
			return src;
		}

		const float x = ExtractFloatComponent(src, 0);
		const float y = ExtractFloatComponent(src, 1);
		const float z = ExtractFloatComponent(src, 2);
		const float w = ExtractFloatComponent(src, 3);

		Engine::MaterialParameterValue out{};
		if (componentCount == 2) {
			out.value = Engine::Vector2{ x, y };
		} else if (componentCount == 3) {
			out.value = Engine::Vector3{ x, y, z };
		} else {
			// 色かベクタかでpack後のバイト列は同じなのでVector4へ正規化する
			out.value = Engine::Vector4{ x, y, z, w };
		}
		return out;
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
		const Engine::MaterialParameterValue& parameter,
		const char* sourceValueTypeName,
		uint32_t layoutSizeInBytes) {

		if (variable.offset >= layoutSizeInBytes || variable.offset >= bytes.size()) {
			return;
		}

		uint32_t count = 1;
		uint32_t writeCount = 0;
		switch (variable.valueType) {
		case D3D_SVT_FLOAT:
			writeCount = WriteScalarArray(bytes, variable, ToFloatArray(parameter, count),
				(std::max)(count, GetDeclaredComponentCount(variable)), layoutSizeInBytes);
			break;
		case D3D_SVT_INT:
			writeCount = WriteScalarArray(bytes, variable, ToIntArray(parameter, count), count,
				layoutSizeInBytes);
			break;
		case D3D_SVT_UINT:
		case D3D_SVT_BOOL:
			writeCount = WriteScalarArray(bytes, variable, ToUIntArray(parameter, count), count,
				layoutSizeInBytes);
			break;
		default:
			Engine::Logger::Output(Engine::LogType::Engine, "[Material] unsupported parameter type. name=" + variable.name);
			break;
		}

		if (IsParameterDebugLogEnabled()) {
			uint32_t floatCount = 0;
			const std::array<float, 4> values = ToFloatArray(parameter, floatCount);
			Engine::Logger::Output(Engine::LogType::Engine,
				"[Material Param Debug] name={} offset={} size={} declaredComponents={} declaredBytes={} valueType={} normalizedType={} writeCount={} values=({}, {}, {}, {})",
				variable.name, variable.offset, variable.size, variable.declaredComponentCount,
				variable.declaredByteSize, sourceValueTypeName, GetParameterValueTypeName(parameter), writeCount,
				values[0], values[1], values[2], values[3]);
		}
	}
}

std::vector<uint8_t> Engine::MaterialParameterBufferBuilder::Build(
	const MaterialAsset& material, const MaterialParameterLayout& layout) {

	const uint32_t layoutSizeInBytes = layout.GetSizeInBytes();
	std::vector<uint8_t> bytes((std::max)(layoutSizeInBytes, 16u), 0);
	const std::vector<ShaderConstantBufferVariable>& variables = layout.GetVariables();
	for (size_t variableIndex = 0; variableIndex < variables.size(); ++variableIndex) {

		const ShaderConstantBufferVariable& variable = variables[variableIndex];

		auto found = material.parameters.find(variable.name);
		if (found == material.parameters.end()) {
			continue;
		}

		const char* sourceValueTypeName = GetParameterValueTypeName(found->second);
		const MaterialParameterValue parameter = NormalizeParameterValueForVariable(variable, found->second);

		// Reflectionのoffsetへ直接詰めることで、HLSL側のパッキングに追従する
		WriteParameterValue(bytes, variable, parameter, sourceValueTypeName, layoutSizeInBytes);
	}
	return bytes;
}

std::vector<uint8_t> Engine::MaterialParameterBufferBuilder::BuildElement(
	const std::unordered_map<std::string, MaterialParameterValue>& defaults,
	const std::unordered_map<std::string, MaterialParameterValue>& overrides,
	const MaterialParameterLayout& layout,
	const TextureResolver& resolveTexture) {

	const uint32_t layoutSizeInBytes = layout.GetSizeInBytes();
	std::vector<uint8_t> bytes((std::max)(layoutSizeInBytes, 16u), 0);
	const std::vector<ShaderConstantBufferVariable>& variables = layout.GetVariables();

	// 1変数分を詰める、AssetID値はテクスチャ扱いでbindless indexへ解決しuintとして書く
	auto writeOne = [&](const ShaderConstantBufferVariable& variable, const MaterialParameterValue& value) {

		if (std::holds_alternative<AssetID>(value.value)) {

			const uint32_t index = resolveTexture ? resolveTexture(variable.name, std::get<AssetID>(value.value)) : 0u;
			if (static_cast<size_t>(variable.offset) + sizeof(uint32_t) <= bytes.size()) {
				std::memcpy(bytes.data() + variable.offset, &index, sizeof(uint32_t));
			}
			return;
		}
		const char* sourceValueTypeName = GetParameterValueTypeName(value);
		const MaterialParameterValue parameter = NormalizeParameterValueForVariable(variable, value);
		WriteParameterValue(bytes, variable, parameter, sourceValueTypeName, layoutSizeInBytes);
		};

	for (const ShaderConstantBufferVariable& variable : variables) {

		// 上書きを優先しなければマテリアル既定値を使う
		auto overrideIt = overrides.find(variable.name);
		if (overrideIt != overrides.end()) {
			writeOne(variable, overrideIt->second);
			continue;
		}
		auto defaultIt = defaults.find(variable.name);
		if (defaultIt != defaults.end()) {
			writeOne(variable, defaultIt->second);
			continue;
		}
		// テクスチャindexはcbuffer内でuintとして現れる、未指定はkNoTextureにしてテクスチャなし分岐へ乗せる
		if (variable.valueType == D3D_SVT_UINT &&
			static_cast<size_t>(variable.offset) + sizeof(uint32_t) <= bytes.size()) {

			const uint32_t noTexture = kNoTextureIndex;
			std::memcpy(bytes.data() + variable.offset, &noTexture, sizeof(uint32_t));
		}
	}
	return bytes;
}
