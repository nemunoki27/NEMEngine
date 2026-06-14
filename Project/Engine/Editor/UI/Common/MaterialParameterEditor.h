#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <string>
#include <type_traits>
#include <unordered_map>

//============================================================================
//	MaterialParameterEditor
//	シェーダーreflectionのcbuffer変数からマテリアルパラメータの編集UIを生成する
//	PostProcessとマテリアルインスペクタで同じ描画を使うため共通化している
//============================================================================
namespace Engine::MaterialParameterEditor {

	// reflectionの成分数を求める、宣言成分数とサイズから安全側に決める
	inline uint32_t GetScalarComponentCount(const ShaderConstantBufferVariable& var) {

		uint32_t count = (std::max)(1u, var.declaredComponentCount);
		if (var.columns > 0) {
			count = (std::max)(count, var.columns);
		}
		if (var.rows > 0 && var.columns > 0) {
			count = (std::max)(count, var.rows * var.columns);
		}
		if (count <= 1 && var.size > sizeof(float)) {
			count = static_cast<uint32_t>(var.size / sizeof(float));
		}
		return (std::min)(count, 4u);
	}

	// 名前から色パラメータかどうかを判定する
	inline bool IsColorParameterName(const std::string& name) {

		return name.find("color") != std::string::npos ||
			name.find("Color") != std::string::npos ||
			name.find("tint") != std::string::npos ||
			name.find("Tint") != std::string::npos;
	}

	// 変数タイプから既定のMaterialParameterValueを生成する
	inline MaterialParameterValue DefaultValueForVariable(const ShaderConstantBufferVariable& var) {

		MaterialParameterValue result{};
		const bool isColor = IsColorParameterName(var.name);
		if (var.valueType == D3D_SVT_FLOAT) {
			const uint32_t componentCount = GetScalarComponentCount(var);
			if (componentCount <= 1) {
				result.value = 0.0f;
			} else if (componentCount == 2) {
				result.value = Vector2{};
			} else if (componentCount == 3) {
				result.value = Vector3{};
			} else {
				if (isColor) {
					result.value = Color4(1.0f, 1.0f, 1.0f, 1.0f);
				} else {
					result.value = Vector4{};
				}
			}
		} else if (var.valueType == D3D_SVT_INT) {
			result.value = int32_t(0);
		} else if (var.valueType == D3D_SVT_UINT) {
			result.value = uint32_t(0);
		} else if (var.valueType == D3D_SVT_BOOL) {
			result.value = false;
		} else {
			result.value = 0.0f;
		}
		return result;
	}

	// バリアントからi番目のfloat成分を取り出し、型が違っても安全に変換する
	inline float ExtractFloatComponent(const MaterialParameterValue& value, int idx) {

		return std::visit([idx](const auto& v) -> float {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, float>) {
				return (idx == 0) ? v : 0.0f;
			} else if constexpr (std::is_same_v<T, Vector2>) {
				return (idx == 0) ? v.x : (idx == 1 ? v.y : 0.0f);
			} else if constexpr (std::is_same_v<T, Vector3>) {
				return (idx == 0) ? v.x : (idx == 1 ? v.y : (idx == 2 ? v.z : 0.0f));
			} else if constexpr (std::is_same_v<T, Vector4>) {
				return (idx == 0) ? v.x : (idx == 1 ? v.y : (idx == 2 ? v.z : (idx == 3 ? v.w : 0.0f)));
			} else if constexpr (std::is_same_v<T, Color4>) {
				return (idx == 0) ? v.r : (idx == 1 ? v.g : (idx == 2 ? v.b : (idx == 3 ? v.a : 0.0f)));
			} else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) {
				return (idx == 0) ? static_cast<float>(v) : 0.0f;
			} else if constexpr (std::is_same_v<T, bool>) {
				return (idx == 0 && v) ? 1.0f : 0.0f;
			} else {
				return 0.0f;
			}
			}, value.value);
	}

	// reflectionの型情報とラベルに基づいてUIを表示し、値を更新したらtrueを返す
	// バリアントの格納型ではなくvar.valueType/成分数を基準にするため型不一致のバグが出ない
	inline bool DrawValueEdit(const ShaderConstantBufferVariable& var, MaterialParameterValue& value,
		const FloatEditSetting& floatSetting = FloatEditSetting{}) {

		const char* label = var.name.c_str();
		const bool isColor = IsColorParameterName(var.name);
		const uint32_t componentCount = GetScalarComponentCount(var);

		if (var.valueType == D3D_SVT_FLOAT) {

			if (componentCount <= 1) {
				float v = ExtractFloatComponent(value, 0);
				if (MyGUI::DragFloat(label, v, floatSetting).valueChanged) {
					value.value = v;
					return true;
				}
			} else if (componentCount == 2) {
				Vector2 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1) };
				if (MyGUI::DragVector2(label, v, floatSetting).valueChanged) {
					value.value = v;
					return true;
				}
			} else if (componentCount == 3) {
				if (isColor) {
					Color3 c{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2) };
					if (MyGUI::ColorEdit(label, c).valueChanged) {
						value.value = Vector3{ c.r, c.g, c.b };
						return true;
					}
				} else {
					Vector3 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2) };
					if (MyGUI::DragVector3(label, v, floatSetting).valueChanged) {
						value.value = v;
						return true;
					}
				}
			} else {
				if (isColor) {
					Color4 c{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2), ExtractFloatComponent(value, 3) };
					if (MyGUI::ColorEdit(label, c).valueChanged) {
						value.value = c;
						return true;
					}
				} else {
					Vector4 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2), ExtractFloatComponent(value, 3) };
					if (MyGUI::DragVector4(label, v, floatSetting).valueChanged) {
						value.value = v;
						return true;
					}
				}
			}
		} else if (var.valueType == D3D_SVT_INT) {
			int32_t v = std::visit([](const auto& val) -> int32_t {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, int32_t>) return val;
				else if constexpr (std::is_same_v<T, uint32_t>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, float>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, bool>) return val ? 1 : 0;
				else return 0;
				}, value.value);
			if (MyGUI::DragInt(label, v).valueChanged) {
				value.value = v;
				return true;
			}
		} else if (var.valueType == D3D_SVT_UINT) {
			int32_t iv = std::visit([](const auto& val) -> int32_t {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, uint32_t>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, int32_t>) return val;
				else if constexpr (std::is_same_v<T, float>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, bool>) return val ? 1 : 0;
				else return 0;
				}, value.value);
			if (MyGUI::DragInt(label, iv).valueChanged) {
				value.value = static_cast<uint32_t>((std::max)(0, iv));
				return true;
			}
		} else if (var.valueType == D3D_SVT_BOOL) {
			bool v = std::visit([](const auto& val) -> bool {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, bool>) return val;
				else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) return val != 0;
				else if constexpr (std::is_same_v<T, float>) return val != 0.0f;
				else return false;
				}, value.value);
			if (MyGUI::Checkbox(label, v)) {
				value.value = v;
				return true;
			}
		}
		return false;
	}

	// reflectionの指定cbufferの変数を列挙して編集UIを描く、値が変わったらtrueを返す
	// parametersに未登録の変数は既定値で補完する、補完自体はsaveを誘発しない
	inline bool DrawReflectedCBufferParameters(const ShaderReflectionInfo& reflection,
		const std::string& cbufferName,
		std::unordered_map<std::string, MaterialParameterValue>& parameters) {

		bool valueChanged = false;
		for (const ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			if (cb.name != cbufferName) {
				continue;
			}
			for (const ShaderConstantBufferVariable& var : cb.variables) {

				auto it = parameters.find(var.name);
				if (it == parameters.end()) {
					// シェーダーが要求するパラメータをUIへ出すため既定値で補完する
					it = parameters.emplace(var.name, DefaultValueForVariable(var)).first;
				}
				if (DrawValueEdit(var, it->second)) {
					valueChanged = true;
				}
			}
		}
		return valueChanged;
	}
} // namespace Engine::MaterialParameterEditor
