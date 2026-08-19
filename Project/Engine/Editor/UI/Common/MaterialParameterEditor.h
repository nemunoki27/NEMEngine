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

	// HLSLの16byte整列用メンバはユーザー編集対象にしない
	inline bool IsInternalPaddingParameter(
		const ShaderConstantBufferVariable& var) {

		size_t nameIndex = 0;
		while (nameIndex < var.name.size() &&
			var.name[nameIndex] == '_') {
			++nameIndex;
		}
		if (var.name.size() - nameIndex < 3) {
			return false;
		}
		const auto toLower = [](char c) {
			return c >= 'A' && c <= 'Z' ?
				static_cast<char>(c + ('a' - 'A')) : c;
			};
		return toLower(var.name[nameIndex]) == 'p' &&
			toLower(var.name[nameIndex + 1]) == 'a' &&
			toLower(var.name[nameIndex + 2]) == 'd';
	}

	// メタデータまたは変数名から色パラメータか判定する
	inline bool IsColorParameter(const ShaderConstantBufferVariable& var) {

		if (var.isColor) {
			return true;
		}
		constexpr char kColor[] = "color";
		auto toLower = [](char c) {
			return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
		};
		return std::search(var.name.begin(), var.name.end(), kColor, kColor + 5,
			[toLower](char lhs, char rhs) { return toLower(lhs) == rhs; }) != var.name.end();
	}

	// テクスチャparamはcbuffer内でbindless indexのuintとして現れるので、名前ではなく型で判定する
	inline bool IsReflectedTextureParam(const ShaderConstantBufferVariable& var) {
		return var.valueType == D3D_SVT_UINT;
	}

	// 変数タイプから既定のMaterialParameterValueを生成する
	inline MaterialParameterValue DefaultValueForVariable(const ShaderConstantBufferVariable& var) {

		MaterialParameterValue result{};
		const bool isColor = IsColorParameter(var);
		if (var.valueType == D3D_SVT_FLOAT) {
			const uint32_t componentCount = Engine::GetVariableComponentCount(var);
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

	// reflectionの型情報とラベルに基づいてUIを表示し、編集結果をValueEditResultで返す
	inline ValueEditResult DrawValueEdit(const ShaderConstantBufferVariable& var, MaterialParameterValue& value,
		const FloatEditSetting& floatSetting = FloatEditSetting{}) {

		if (IsInternalPaddingParameter(var)) {
			return ValueEditResult{};
		}
		const char* label = var.name.c_str();
		const bool isColor = IsColorParameter(var);
		const uint32_t componentCount = Engine::GetVariableComponentCount(var);

		if (var.valueType == D3D_SVT_FLOAT) {

			if (componentCount <= 1) {
				float v = ExtractFloatComponent(value, 0);
				ValueEditResult result = MyGUI::DragFloat(label, v, floatSetting);
				if (result.valueChanged) { value.value = v; }
				return result;
			} else if (componentCount == 2) {
				Vector2 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1) };
				ValueEditResult result = MyGUI::DragVector2(label, v, floatSetting);
				if (result.valueChanged) { value.value = v; }
				return result;
			} else if (componentCount == 3) {
				if (isColor) {
					Color3 c{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2) };
					ValueEditResult result = MyGUI::ColorEdit(label, c);
					if (result.valueChanged) { value.value = Vector3{ c.r, c.g, c.b }; }
					return result;
				} else {
					Vector3 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2) };
					ValueEditResult result = MyGUI::DragVector3(label, v, floatSetting);
					if (result.valueChanged) { value.value = v; }
					return result;
				}
			} else {
				if (isColor) {
					Color4 c{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2), ExtractFloatComponent(value, 3) };
					ValueEditResult result = MyGUI::ColorEdit(label, c);
					if (result.valueChanged) { value.value = c; }
					return result;
				} else {
					Vector4 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2), ExtractFloatComponent(value, 3) };
					ValueEditResult result = MyGUI::DragVector4(label, v, floatSetting);
					if (result.valueChanged) { value.value = v; }
					return result;
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
			ValueEditResult result = MyGUI::DragInt(label, v);
			if (result.valueChanged) { value.value = v; }
			return result;
		} else if (var.valueType == D3D_SVT_UINT) {
			int32_t iv = std::visit([](const auto& val) -> int32_t {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, uint32_t>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, int32_t>) return val;
				else if constexpr (std::is_same_v<T, float>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, bool>) return val ? 1 : 0;
				else return 0;
				}, value.value);
			ValueEditResult result = MyGUI::DragInt(label, iv);
			if (result.valueChanged) { value.value = static_cast<uint32_t>((std::max)(0, iv)); }
			return result;
		} else if (var.valueType == D3D_SVT_BOOL) {
			bool v = std::visit([](const auto& val) -> bool {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, bool>) return val;
				else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) return val != 0;
				else if constexpr (std::is_same_v<T, float>) return val != 0.0f;
				else return false;
				}, value.value);
			// Checkboxはトグルが即確定なのでvalueChangedとeditFinishedを同時に立てる
			ValueEditResult result{};
			if (MyGUI::Checkbox(label, v)) {
				value.value = v;
				result.valueChanged = true;
				result.editFinished = true;
			}
			return result;
		}
		return ValueEditResult{};
	}

	// reflectionの指定cbufferの変数を列挙して編集UIを描く、値が変わったらtrueを返す
	// parametersに未登録の変数は既定値で補完する、補完自体はsaveを誘発しない
	inline bool DrawReflectedCBufferParameters(const ShaderReflectionInfo& reflection,
		const std::string& cbufferName,
		MaterialParameterSet& parameters) {

		bool valueChanged = false;
		for (const ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			if (cb.name != cbufferName) {
				continue;
			}
			for (const ShaderConstantBufferVariable& var : cb.variables) {
				if (IsInternalPaddingParameter(var)) {
					continue;
				}

				MaterialParameterValue* value =
					parameters.Find(var.parameterID);
				if (!value) {
					// シェーダーが要求するパラメータをUIへ出すため既定値で補完する
					parameters.Set(
						var.parameterID, var.name, var.semantic,
						DefaultValueForVariable(var));
					value = parameters.Find(var.parameterID);
				}
				if (value && DrawValueEdit(var, *value).valueChanged) {
					valueChanged = true;
				}
			}
		}
		return valueChanged;
	}
} // namespace Engine::MaterialParameterEditor
