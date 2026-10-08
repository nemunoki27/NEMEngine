#include "MaterialParameterBufferBuilder.h"

//============================================================================
//	include
//============================================================================
#include "MaterialParameterLookup.h"
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <type_traits>
#include <variant>

//============================================================================
//	MaterialParameterBufferBuilder classMethods
//============================================================================
namespace {

	// 宣言領域と配置領域に収まる成分数を取得する
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

	// 未使用成分も宣言された成分数で扱う
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

	// 診断用に保存値の型名を取得する
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

	// Debug設定で詳細ログを切り替える
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

	// 宣言された成分だけをoffsetへ書き込む
	template<typename TValue>
	uint32_t WriteScalarArray(std::span<uint8_t> bytes,
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

	// 数値とVectorと色を浮動小数点の成分へ変換する
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

	// 存在しない成分はゼロとして取得する
	float ExtractFloatComponent(const Engine::MaterialParameterValue& parameter, uint32_t index) {

		uint32_t count = 0;
		const std::array<float, 4> values = ToFloatArray(parameter, count);
		return index < values.size() ? values[index] : 0.0f;
	}

	// 宣言されたVector幅へ値を揃える
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

	// Boolと数値を符号なし整数の成分へ変換する
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
				// 変換できない値は未定義動作を避けてゼロにする
				uint32_t converted = 0;
				Math::TryConvertToUInt32(value, converted);
				return { converted, 0u, 0u, 0u };
			} else {
				return {};
			}
			}, parameter.value);
	}

	// Boolと数値を符号付き整数の成分へ変換する
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
				// 変換できない値は未定義動作を避けてゼロにする
				int32_t converted = 0;
				Math::TryConvertToInt32(value, converted);
				return { converted, 0, 0, 0 };
			} else {
				return {};
			}
			}, parameter.value);
	}

	// Shaderの宣言型で値を書き込む
	void WriteParameterValue(std::span<uint8_t> bytes,
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
			Engine::Logger::Output(Engine::LogType::Engine,
				"[Material] 未対応のパラメーター型です name=" + variable.name);
			break;
		}

		if (IsParameterDebugLogEnabled()) {
			uint32_t floatCount = 0;
			const std::array<float, 4> values = ToFloatArray(parameter, floatCount);
			Engine::Logger::Output(Engine::LogType::Engine,
				"[Material Parameter Debug] 名前={} offset={} size={} 宣言要素数={} 宣言Byte数={} "
				"値型={} 正規化型={} 書き込み数={} 値=({}, {}, {}, {})",
				variable.name, variable.offset, variable.size, variable.declaredComponentCount,
				variable.declaredByteSize, sourceValueTypeName, GetParameterValueTypeName(parameter), writeCount,
				values[0], values[1], values[2], values[3]);
		}
	}

	// MaterialのIDと用途と名前で既定値を探す
	const Engine::MaterialParameterValue* FindParameterValue(
		const Engine::MaterialParameterSet& parameters,
		const Engine::ShaderConstantBufferVariable& variable) {

		return Engine::MaterialParameterLookup::Find(parameters, variable.parameterID, variable.semantic, variable.name);
	}

	// 明示IDと既定値の名前でInstanceの上書きを探す
	const Engine::MaterialParameterValue* FindOverrideParameterValue(
		const Engine::MaterialParameterSet& defaults,
		const Engine::MaterialParameterSet& overrides,
		const Engine::ShaderConstantBufferVariable& variable) {

		return Engine::MaterialParameterLookup::Find(overrides, variable.parameterID, variable.semantic, variable.name, &defaults);
	}

}

std::vector<uint8_t> Engine::MaterialParameterBufferBuilder::Build(
	const MaterialAsset& material, const MaterialParameterLayout& layout,
	const TextureResolver& resolveTexture,
	bool* outTextureValuesCacheable) {

	// 既定値だけの構築も共通経路へ揃える
	return BuildElement(material.parameters, MaterialParameterSet{}, layout, resolveTexture, outTextureValuesCacheable);
}

bool Engine::MaterialParameterBufferBuilder::BuildInto(std::span<uint8_t> bytes,
	const MaterialAsset& material, const MaterialParameterLayout& layout,
	const TextureResolver& resolveTexture,
	bool* outTextureValuesCacheable) {

	static const MaterialParameterSet kEmptyOverrides{};
	return BuildElementInto(bytes, material.parameters,
		kEmptyOverrides, layout, resolveTexture,
		outTextureValuesCacheable);
}

std::vector<uint8_t> Engine::MaterialParameterBufferBuilder::BuildElement(
	const MaterialParameterSet& defaults,
	const MaterialParameterSet& overrides,
	const MaterialParameterLayout& layout,
	const TextureResolver& resolveTexture,
	bool* outTextureValuesCacheable) {

	const uint32_t layoutSizeInBytes = layout.GetSizeInBytes();
	std::vector<uint8_t> bytes((std::max)(layoutSizeInBytes, 16u), 0);
	BuildElementInto(bytes, defaults, overrides, layout, resolveTexture,
		outTextureValuesCacheable);
	return bytes;
}

bool Engine::MaterialParameterBufferBuilder::BuildElementInto(std::span<uint8_t> bytes,
	const MaterialParameterSet& defaults,
	const MaterialParameterSet& overrides,
	const MaterialParameterLayout& layout,
	const TextureResolver& resolveTexture,
	bool* outTextureValuesCacheable) {

	const uint32_t layoutSizeInBytes = layout.GetSizeInBytes();
	if (bytes.size() < (std::max)(layoutSizeInBytes, 16u)) {
		return false;
	}
	std::fill(bytes.begin(), bytes.end(), uint8_t{});
	bool textureValuesCacheable = true;

	const std::vector<ShaderConstantBufferVariable>& variables = layout.GetVariables();

	// AssetIDをTexture番号へ解決して宣言位置へ書き込む
	auto writeOne = [&](const ShaderConstantBufferVariable& variable, const MaterialParameterValue& value) {

		if (std::holds_alternative<AssetID>(value.value)) {

			TextureResolveResult result{};
			if (resolveTexture) {
				result = resolveTexture(variable.semantic,
					std::get<AssetID>(value.value));
			} else if (std::get<AssetID>(value.value)) {
				result.cacheable = false;
			}
			textureValuesCacheable &= result.cacheable;
			if (static_cast<size_t>(variable.offset) + sizeof(uint32_t) <= bytes.size()) {
				std::memcpy(bytes.data() + variable.offset,
					&result.index, sizeof(uint32_t));
			}
			return;
		}
		const char* sourceValueTypeName = GetParameterValueTypeName(value);
		const MaterialParameterValue parameter = NormalizeParameterValueForVariable(variable, value);
		WriteParameterValue(bytes, variable, parameter, sourceValueTypeName, layoutSizeInBytes);
		};

	for (const ShaderConstantBufferVariable& variable : variables) {

		// 上書きを優先し、未指定なら既定値を使う
		if (const MaterialParameterValue* overrideValue =
			FindOverrideParameterValue(defaults, overrides, variable)) {
			writeOne(variable, *overrideValue);
			continue;
		}
		if (const MaterialParameterValue* defaultValue =
			FindParameterValue(defaults, variable)) {
			writeOne(variable, *defaultValue);
			continue;
		}
		// Textureだけを未指定扱いにし、通常のuintはゼロを維持する
		if (variable.isTexture && variable.valueType == D3D_SVT_UINT &&
			static_cast<size_t>(variable.offset) + sizeof(uint32_t) <= bytes.size()) {

			const uint32_t noTexture = kNoTextureIndex;
			std::memcpy(bytes.data() + variable.offset, &noTexture, sizeof(uint32_t));
		}
	}
	if (outTextureValuesCacheable) {
		*outTextureValuesCacheable = textureValuesCacheable;
	}
	return true;
}

uint64_t Engine::MaterialParameterBufferBuilder::ComputeHash(const MaterialParameterSet& parameters) {

	return parameters.GetContentHash();
}
