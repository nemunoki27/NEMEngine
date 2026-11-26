#include "CameraSurfaceSettingsEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/CameraSurfacePipeline.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>

// c++
#include <algorithm>
#include <variant>

namespace {

	Engine::ValueEditResult ItemEdit(bool changed) {

		return { changed, ImGui::IsItemActive(), ImGui::IsItemDeactivatedAfterEdit() };
	}

	Engine::ValueEditResult DrawQuality(int32_t& quality) {

		return ItemEdit(ImGui::Combo("品質", &quality, "軽量\0標準\0高品質\0"));
	}

	Engine::ValueEditResult DrawPassParameters(Engine::MaterialInstanceParameters& overrides,
		const Engine::MaterialParameterSet& defaults) {

		Engine::ValueEditResult result{};
		for (const auto& record : defaults.GetRecords()) {
			const auto& [name, parameter] = record.namedValue;
			const auto* fallback = std::get_if<float>(&parameter.value);
			if (!fallback) continue;
			const auto* stored = overrides.Find(record.id);
			const auto* scalar = stored ? std::get_if<float>(&stored->value) : nullptr;
			float value = scalar ? *scalar : *fallback;
			const auto edit = ItemEdit(ImGui::DragFloat(name.c_str(), &value, 0.01f));
			if (edit.valueChanged) overrides.Set(record.id, name, record.semantic, Engine::MaterialParameterValue{ .value = value });
			result.Merge(edit);
			if (ImGui::BeginPopupContextItem()) {
				Engine::MaterialParameterEditor::DrawParameterCopyMenu(record.id, name);
				ImGui::EndPopup();
			}
		}
		return result;
	}
}

//============================================================================
//	CameraSurfaceSettingsEditor classMethods
//============================================================================
Engine::ValueEditResult Engine::CameraSurfaceSettingsEditor::DrawReflection(SurfaceLightingSettings& settings, RenderAssetLibrary* library) {

	ValueEditResult result{};
	result.Merge(ItemEdit(ImGui::Checkbox("反射を使用", &settings.reflectionEnabled)));
	ImGui::BeginDisabled(!settings.reflectionEnabled);
	result.Merge(DrawQuality(settings.reflectionQuality));
	result.Merge(ItemEdit(ImGui::DragFloat("反射強度", &settings.reflectionIntensity, 0.01f, 0.0f, 65504.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)));
	result.Merge(ItemEdit(ImGui::DragFloat("最大距離", &settings.reflectionMaxDistance, 0.1f, 0.01f, 100000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)));
	if (library && ImGui::BeginMenu("詳細設定")) {
		static const auto profile = BuildCameraReflectionProfile(SurfaceLightingSettings{});
		for (const auto& pass : profile.passes) {
			const auto* material = library->LoadMaterial(pass.material);
			if (!material || (material->parameters.empty() && pass.parameterOverrides.empty()) || !ImGui::BeginMenu(pass.name.c_str())) continue;
			auto defaults = material->parameters;
			defaults.MergeFrom(pass.parameterOverrides);
			const std::string id = ToString(pass.id);
			auto& passes = settings.reflectionParameters["passes"];
			if (!passes.is_array()) passes = nlohmann::json::array();
			auto saved = std::find_if(passes.begin(), passes.end(), [&](const auto& entry) {
				return entry.is_object() && entry.contains("id") && entry["id"] == id;
			});
			MaterialInstanceParameters overrides;
			if (saved != passes.end() && saved->contains("parameters")) ReadMaterialInstance((*saved)["parameters"], overrides);
			const auto edit = DrawPassParameters(overrides, defaults);
			result.Merge(edit);
			if (edit.valueChanged) {
				if (saved == passes.end()) passes.push_back({{"id", id}, {"name", pass.name}, {"parameters", WriteMaterialInstance(overrides)}});
				else (*saved)["parameters"] = WriteMaterialInstance(overrides);
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
	ImGui::EndDisabled();
	return result;
}

Engine::ValueEditResult Engine::CameraSurfaceSettingsEditor::DrawAmbientOcclusion(
	SurfaceLightingSettings& settings, GraphicsCore* graphicsCore) {

	ValueEditResult result{};
	const bool supported = graphicsCore && graphicsCore->GetDXObject().GetFeatureController().GetSupport().SupportsInlineRayTracingPath() &&
		graphicsCore->GetDXObject().GetFeatureController().GetSupport().SupportsShaderModel6_6();
	ImGui::BeginDisabled(!supported);
	result.Merge(ItemEdit(ImGui::Checkbox("AOを使用", &settings.ambientOcclusionEnabled)));
	ImGui::BeginDisabled(!settings.ambientOcclusionEnabled);
	result.Merge(DrawQuality(settings.ambientOcclusionQuality));
	result.Merge(ItemEdit(ImGui::DragFloat("強度", &settings.ambientOcclusionIntensity, 0.01f, 0.0f, 65504.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)));
	result.Merge(ItemEdit(ImGui::DragFloat("半径", &settings.ambientOcclusionRadius, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)));
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	if (!supported) ImGui::TextDisabled("使用不可");
	return result;
}
