#include "MaterialParameterEditor.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <array>
#include <type_traits>
#include <unordered_map>

//============================================================================
//	MaterialParameterEditor classMethods
//============================================================================

namespace Engine::MaterialParameterEditor {

	MyGUI::ScopedPropertyLabelContextMenu MakeLabelContextMenu(
		MaterialParameterID parameterID, std::string_view name) {

		return MyGUI::ScopedPropertyLabelContextMenu([parameterID, name]() {

			if (ImGui::MenuItem("バッファ名をコピー")) {
				ImGui::SetClipboardText(std::string(name).c_str());
			}
			if (ImGui::MenuItem("マテリアルIDをコピー", nullptr, false, static_cast<bool>(parameterID))) {
				ImGui::SetClipboardText(Engine::ToString(UUID{ parameterID.value }).c_str());
			}
			});
	}

	size_t GetScalarDisplayRank(
		const ShaderConstantBufferVariable& var) {

		static constexpr std::array kOrder{
			MaterialParameterSemantic::BaseColor,
			MaterialParameterSemantic::EmissiveColor,
			MaterialParameterSemantic::EmissiveIntensity,
			MaterialParameterSemantic::Metallic,
			MaterialParameterSemantic::Roughness,
			MaterialParameterSemantic::AmbientOcclusion,
			MaterialParameterSemantic::Opacity,
			MaterialParameterSemantic::AlphaClip,
			MaterialParameterSemantic::DisplacementMidpoint,
			MaterialParameterSemantic::DisplacementScale,
		};
		for (size_t i = 0; i < kOrder.size(); ++i) {
			if (var.semantic == kOrder[i]) {
				return i;
			}
		}
		return kOrder.size();
	}

	size_t GetTextureDisplayRank(
		MaterialParameterSemantic semantic, std::string_view name) {

		static constexpr std::array kOrder{
			MaterialParameterSemantic::BaseColorTexture,
			MaterialParameterSemantic::NormalTexture,
			MaterialParameterSemantic::MetallicRoughnessTexture,
			MaterialParameterSemantic::RoughnessTexture,
			MaterialParameterSemantic::MetallicTexture,
			MaterialParameterSemantic::EmissiveTexture,
			MaterialParameterSemantic::AmbientOcclusionTexture,
			MaterialParameterSemantic::DisplacementTexture,
		};
		if (semantic == MaterialParameterSemantic::None) {
			semantic = ResolveMaterialParameterSemantic(name);
		}
		for (size_t i = 0; i < kOrder.size(); ++i) {
			if (semantic == kOrder[i]) {
				return i;
			}
		}
		return kOrder.size();
	}

	size_t GetTextureDisplayRank(std::string_view name) {

		return GetTextureDisplayRank(
			MaterialParameterSemantic::None, name);
	}

	void SortScalarParametersForDisplay(
		std::vector<const ShaderConstantBufferVariable*>& variables) {

		std::stable_sort(variables.begin(), variables.end(),
			[](const ShaderConstantBufferVariable* lhs,
				const ShaderConstantBufferVariable* rhs) {
				return GetScalarDisplayRank(*lhs) <
					GetScalarDisplayRank(*rhs);
			});
	}

	void SortTextureParametersForDisplay(
		std::vector<const ShaderConstantBufferVariable*>& variables) {

		std::stable_sort(variables.begin(), variables.end(),
			[](const ShaderConstantBufferVariable* lhs,
				const ShaderConstantBufferVariable* rhs) {
				return GetTextureDisplayRank(lhs->name) <
					GetTextureDisplayRank(rhs->name);
			});
	}

	bool IsInternalPaddingParameter(
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

	bool IsColorParameter(const ShaderConstantBufferVariable& var) {

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

	bool IsMaterialTextureResource(
		const ShaderResourceBinding& resource) {

		return resource.kind == ShaderBindingKind::SRV &&
			resource.space == 2 &&
			resource.rawType == D3D_SIT_TEXTURE;
	}

	bool IsTextureSemantic(
		MaterialParameterSemantic semantic) {

		switch (semantic) {
		case MaterialParameterSemantic::BaseColorTexture:
		case MaterialParameterSemantic::NormalTexture:
		case MaterialParameterSemantic::MetallicRoughnessTexture:
		case MaterialParameterSemantic::RoughnessTexture:
		case MaterialParameterSemantic::MetallicTexture:
		case MaterialParameterSemantic::EmissiveTexture:
		case MaterialParameterSemantic::AmbientOcclusionTexture:
		case MaterialParameterSemantic::DisplacementTexture:
			return true;
		default:
			return false;
		}
	}

	bool IsSameReflectedMaterialParameter(
		const ShaderConstantBufferVariable& variable,
		const ShaderResourceBinding& resource) {

		if (variable.parameterID && resource.parameterID) {
			return variable.parameterID == resource.parameterID;
		}
		if (variable.semantic != MaterialParameterSemantic::None &&
			resource.semantic != MaterialParameterSemantic::None) {
			return variable.semantic == resource.semantic;
		}
		return variable.name == resource.name;
	}

	const ShaderResourceBinding* FindReflectedTextureResource(
		const ShaderConstantBufferVariable& variable,
		const ShaderReflectionInfo& reflection) {

		for (const ShaderResourceBinding& resource : reflection.resources) {
			if (IsMaterialTextureResource(resource) &&
				IsSameReflectedMaterialParameter(variable, resource)) {
				return &resource;
			}
		}
		return nullptr;
	}

	const ShaderConstantBufferVariable* FindReflectedTextureParameter(
		const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection) {

		for (const ShaderConstantBufferInfo& buffer : reflection.constantBuffers) {
			for (const ShaderConstantBufferVariable& variable : buffer.variables) {
				if (IsSameReflectedMaterialParameter(variable, resource)) {
					return &variable;
				}
			}
		}
		for (const ShaderStructuredBufferInfo& buffer : reflection.structuredBuffers) {
			for (const ShaderConstantBufferVariable& variable : buffer.variables) {
				if (IsSameReflectedMaterialParameter(variable, resource)) {
					return &variable;
				}
			}
		}
		return nullptr;
	}

	bool IsReflectedTextureParam(
		const ShaderConstantBufferVariable& variable,
		const ShaderReflectionInfo& reflection) {

		if (variable.valueType != D3D_SVT_UINT) {
			return false;
		}
		return variable.isTexture ||
			IsTextureSemantic(variable.semantic) ||
			IsTextureSemantic(ResolveMaterialParameterSemantic(variable.name)) ||
			FindReflectedTextureResource(variable, reflection) != nullptr;
	}

	std::string_view GetReflectedTextureDisplayName(
		const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection) {

		if (const ShaderConstantBufferVariable* variable =
			FindReflectedTextureParameter(resource, reflection)) {
			return variable->name;
		}
		return resource.name;
	}

	MaterialParameterID GetReflectedTextureParameterID(
		const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection) {

		if (resource.parameterID) {
			return resource.parameterID;
		}
		if (const ShaderConstantBufferVariable* variable =
			FindReflectedTextureParameter(resource, reflection)) {
			if (variable->parameterID) {
				return variable->parameterID;
			}
		}
		return MaterialParameterID::FromName(resource.name);
	}

	MaterialParameterSemantic GetReflectedTextureSemantic(
		const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection) {

		if (resource.semantic != MaterialParameterSemantic::None) {
			return resource.semantic;
		}
		if (const ShaderConstantBufferVariable* variable =
			FindReflectedTextureParameter(resource, reflection)) {
			return variable->semantic;
		}
		return ResolveMaterialParameterSemantic(resource.name);
	}

	MaterialParameterValue DefaultValueForVariable(const ShaderConstantBufferVariable& var) {

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

	float ExtractFloatComponent(const MaterialParameterValue& value, int idx) {

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

	ValueEditResult DrawValueEdit(const ShaderConstantBufferVariable& var, MaterialParameterValue& value,
		const FloatEditSetting& floatSetting) {

		if (IsInternalPaddingParameter(var)) {
			return ValueEditResult{};
		}
		const char* label = var.name.c_str();
		const auto labelContextMenu = MakeLabelContextMenu(var.parameterID, var.name);
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

	bool DrawReflectedCBufferParameters(const ShaderReflectionInfo& reflection,
		const std::string& cbufferName,
		MaterialParameterSet& parameters) {

		bool valueChanged = false;
		for (const ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			if (cb.name != cbufferName) {
				continue;
			}
			for (const ShaderConstantBufferVariable& var : cb.variables) {
				if (IsInternalPaddingParameter(var) ||
					IsReflectedTextureParam(var, reflection)) {
					continue;
				}

				const MaterialParameterValue* value =
					parameters.Find(var.parameterID);
				if (!value) {
					// シェーダーが要求するパラメータをUIへ出すため既定値で補完する
					parameters.Set(
						var.parameterID, var.name, var.semantic,
						DefaultValueForVariable(var));
					value = parameters.Find(var.parameterID);
				}
				if (value) {
					MaterialParameterValue draft = *value;
					if (DrawValueEdit(var, draft).valueChanged) {
						parameters.Set(var.parameterID, var.name, var.semantic, draft);
						valueChanged = true;
					}
				}
			}
		}
		return valueChanged;
	}
}
