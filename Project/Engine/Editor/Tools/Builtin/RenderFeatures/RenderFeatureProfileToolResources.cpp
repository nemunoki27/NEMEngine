#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureInputSources.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>

namespace {

	// 出力定義は保持するが、用途が固まるまで追加削除UIを隠す
	constexpr bool kShowOutputCollectionEditing = false;
}

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================
std::string Engine::RenderFeatureProfileTool::MakeReferenceLabel(
	const RenderFeatureProfileAsset& profile,
	const RenderFeatureOutputReference& reference,
	const char* emptyLabel) {

	if (!reference.pass) {
		return emptyLabel;
	}
	const auto found = std::find_if(profile.passes.begin(),
		profile.passes.end(), [&](const RenderFeaturePassSettings& pass) {

			return pass.id == reference.pass;
		});
	const std::string_view passName = found == profile.passes.end() ?
		"(なし)" : std::string_view(found->name);
	return std::string(passName) + " / " +
		(reference.output.empty() ? "Color" : reference.output);
}

bool Engine::RenderFeatureProfileTool::DrawOutputReferenceCombo(
	const char* label, const RenderFeatureProfileAsset& profile,
	const RenderFeaturePassSettings& owner,
	RenderFeatureOutputReference& reference, const char* emptyLabel) {

	bool changed = false;
	const std::string preview = MakeReferenceLabel(
		profile, reference, emptyLabel);
	if (!ImGui::BeginCombo(label, preview.c_str())) {
		return false;
	}
	if (ImGui::Selectable(emptyLabel, !reference.pass)) {
		reference = {};
		changed = true;
	}
	for (const RenderFeaturePassSettings& candidate : profile.passes) {

		if (!candidate.enabled || candidate.id == owner.id ||
			GetRenderFeatureAnchorOrder(candidate.anchor) >
				GetRenderFeatureAnchorOrder(owner.anchor)) {

			continue;
		}
		const std::vector<RenderFeatureOutputSettings> outputs =
			candidate.outputs.empty() ?
				std::vector<RenderFeatureOutputSettings>{
					RenderFeatureOutputSettings{} } : candidate.outputs;
		for (const RenderFeatureOutputSettings& output : outputs) {

			const bool selected = reference.pass == candidate.id &&
				reference.output == output.name;
			const std::string item = candidate.name + " / " + output.name;
			if (ImGui::Selectable(item.c_str(), selected)) {
				reference.pass = candidate.id;
				reference.output = output.name;
				changed = true;
			}
		}
	}
	ImGui::EndCombo();
	return changed;
}

bool Engine::RenderFeatureProfileTool::DrawSamplerSettings(
	PipelineStaticSamplerSettings& settings) {

	bool changed = false;
	changed |= MyGUI::EnumCombo("フィルタ", settings.filter).valueChanged;
	changed |= MyGUI::EnumCombo("アドレスU", settings.addressU).valueChanged;
	changed |= MyGUI::EnumCombo("アドレスV", settings.addressV).valueChanged;
	changed |= MyGUI::EnumCombo("アドレスW", settings.addressW).valueChanged;
	changed |= MyGUI::EnumCombo("比較関数",
		settings.comparisonFunc).valueChanged;
	changed |= MyGUI::EnumCombo("境界色", settings.borderColor).valueChanged;
	int32_t anisotropy = static_cast<int32_t>(settings.maxAnisotropy);
	if (MyGUI::DragInt("異方性", anisotropy,
		{ .minValue = 1, .maxValue = 16 }).valueChanged) {

		settings.maxAnisotropy = static_cast<uint32_t>(anisotropy);
		changed = true;
	}
	changed |= MyGUI::DragFloat("Mip LODバイアス",
		settings.mipLODBias).valueChanged;
	changed |= MyGUI::DragFloat("最小LOD", settings.minLOD).valueChanged;
	changed |= MyGUI::DragFloat("最大LOD", settings.maxLOD).valueChanged;
	return changed;
}

void Engine::RenderFeatureProfileTool::DrawOutputs(
	RenderFeaturePassSettings& pass) {

	if (!MyGUI::CollapsingHeader("出力", false)) {
		return;
	}
	bool changed = false;
	ImGui::Indent();
	for (size_t index = 0; index < pass.outputs.size();) {
		RenderFeatureOutputSettings& output = pass.outputs[index];
		ImGui::PushID(static_cast<int32_t>(index));
		if (MyGUI::CollapsingHeader(output.name.c_str(), false)) {
			MyGUI::ScopedPropertyLabelWidth width("RenderFeatureOutput");
			changed |= MyGUI::InputText("名前", output.name).valueChanged;
			changed |= MyGUI::InputText("UAV名", output.shaderResource).valueChanged;
			changed |= MyGUI::EnumCombo("フォーマット", output.format).valueChanged;
			changed |= MyGUI::DragFloat("幅スケール", output.widthScale,
				{ .minValue = 0.0625f, .maxValue = 4.0f }).valueChanged;
			changed |= MyGUI::DragFloat("高さスケール", output.heightScale,
				{ .minValue = 0.0625f, .maxValue = 4.0f }).valueChanged;
			changed |= MyGUI::Checkbox("履歴を保持", output.history);
			if (output.history) {
				changed |= MyGUI::InputText("履歴SRV名",
					output.historyShaderResource).valueChanged;
			}
			bool clear = output.clearColor.has_value();
			if (MyGUI::Checkbox("実行前にクリア", clear)) {
				output.clearColor = clear ?
					std::optional<Color4>{ Color4::Black() } : std::nullopt;
				changed = true;
			}
			if (output.clearColor.has_value()) {
				Color4 color = *output.clearColor;
				if (MyGUI::ColorEdit("クリア色", color).valueChanged) {
					output.clearColor = color;
					changed = true;
				}
			}
			if (kShowOutputCollectionEditing &&
				ImGui::Button("出力を削除")) {

				pass.outputs.erase(pass.outputs.begin() + index);
				changed = true;
				ImGui::PopID();
				continue;
			}
		}
		ImGui::PopID();
		++index;
	}
	if (kShowOutputCollectionEditing && ImGui::Button("出力を追加")) {
		RenderFeatureOutputSettings output{};
		output.name += std::to_string(pass.outputs.size());
		output.shaderResource += std::to_string(pass.outputs.size());
		pass.outputs.emplace_back(std::move(output));
		changed = true;
	}
	ImGui::Unindent();
	if (changed) {
		SetDirty();
	}
}

void Engine::RenderFeatureProfileTool::DrawResources(
	const EditorToolContext& context, RenderFeaturePassSettings& pass) {

	if (!MyGUI::CollapsingHeader("リソースとパラメータ", false)) {
		return;
	}
	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	RenderPipelineRunner* renderPipeline = context.panelContext ?
		context.panelContext->renderPipeline : nullptr;
	GraphicsCore* graphicsCore = context.panelContext ?
		context.panelContext->graphicsCore : nullptr;
	if (pass.material && renderPipeline && graphicsCore &&
		!service.FindReflectionResources(pass.material, pass.materialPass)) {

		std::vector<ShaderConstantBufferVariable> variables{};
		std::vector<ShaderResourceBinding> resources{};
		std::vector<ShaderResourceBinding> samplers{};
		bool reflected = false;
		if (pass.type == RenderFeaturePassType::Compute) {
			reflected = renderPipeline->TryGetMaterialComputeReflection(
				*graphicsCore, pass.material, pass.materialPass,
				variables, resources, samplers);
		} else if (const ShaderReflectionInfo* reflection =
			renderPipeline->FindMaterialRayTracingReflection(
				*graphicsCore, pass.material)) {

			reflected = true;
			for (const ShaderConstantBufferInfo& buffer :
				reflection->constantBuffers) {

				if (buffer.name == "RayTracingParameters") {
					variables = buffer.variables;
					break;
				}
			}
			for (const ShaderResourceBinding& binding : reflection->resources) {
				(binding.kind == ShaderBindingKind::Sampler ?
					samplers : resources).emplace_back(binding);
			}
		}
		if (reflected) {
			service.CacheReflection(pass.material, pass.materialPass,
				variables, resources, samplers);
		}
	}
	if (ImGui::Button("シェーダーを再読み込み",
		ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)) &&
		pass.material && renderPipeline) {

		service.ClearReflection(pass.material);
		renderPipeline->ReloadMaterial(pass.material);
	}

	bool changed = false;
	if (const auto* variables =
		service.FindReflectionVariables(pass.material, pass.materialPass)) {

		const MaterialAsset* material = renderPipeline ?
			renderPipeline->GetRenderAssetLibrary().LoadMaterial(pass.material) :
			nullptr;
		for (const ShaderConstantBufferVariable& variable : *variables) {
			if (MaterialParameterEditor::IsInternalPaddingParameter(variable)) {
				continue;
			}
			MaterialParameterValue value =
				MaterialParameterEditor::DefaultValueForVariable(variable);
			if (const MaterialParameterValue* materialValue = material ?
				material->parameters.Find(variable.parameterID) : nullptr) {

				value = *materialValue;
			}
			if (const MaterialParameterValue* overrideValue =
				pass.parameterOverrides.Find(variable.parameterID)) {

				value = *overrideValue;
			}
			if (MaterialParameterEditor::DrawValueEdit(
				variable, value).valueChanged) {

				pass.parameterOverrides.Set(variable.parameterID,
					variable.name, variable.semantic, value);
				changed = true;
			}
		}
	}

	if (const auto* resources = service.FindReflectionResources(
		pass.material, pass.materialPass)) {
		const RenderFeatureProfileAsset& profile = service.GetProfile();
		for (const ShaderResourceBinding& binding : *resources) {
			if (binding.kind != ShaderBindingKind::SRV || binding.name.empty()) {
				continue;
			}
			ImGui::PushID(binding.name.c_str());
			if (binding.name == PostProcessBindingNames::kSourceColor) {
				if (MyGUI::BeginPropertyRow("入力元")) {
					ImGui::TextUnformatted("主入力");
					MyGUI::EndPropertyRow();
				}
				ImGui::PopID();
				continue;
			}

			const auto sceneInput = pass.sceneInputs.find(binding.name);
			const bool hasSceneInput = sceneInput != pass.sceneInputs.end();
			const std::string sceneInputValue = hasSceneInput ?
				sceneInput->second : std::string{};
			const auto passInput = pass.passInputs.find(binding.name);
			const bool hasPassInput = passInput != pass.passInputs.end();
			const RenderFeatureOutputReference passInputValue = hasPassInput ?
				passInput->second : RenderFeatureOutputReference{};
			std::string inputPreview = "テクスチャ";
			if (hasSceneInput) {
				inputPreview = sceneInputValue;
			} else if (hasPassInput) {
				inputPreview = MakeReferenceLabel(profile,
					passInputValue, "未設定");
			}
			if (ImGui::BeginCombo("入力元", inputPreview.c_str())) {
				if (ImGui::Selectable("テクスチャ",
					!hasSceneInput && !hasPassInput)) {

					pass.sceneInputs.erase(binding.name);
					pass.passInputs.erase(binding.name);
					changed = true;
				}
				for (const char* source : kRenderFeatureInputSources) {
					if (ImGui::Selectable(source,
						hasSceneInput && sceneInputValue == source)) {

						pass.sceneInputs[binding.name] = source;
						pass.passInputs.erase(binding.name);
						changed = true;
					}
				}
				for (const RenderFeaturePassSettings& candidate : profile.passes) {

					if (!candidate.enabled || candidate.id == pass.id ||
						GetRenderFeatureAnchorOrder(candidate.anchor) >
							GetRenderFeatureAnchorOrder(pass.anchor)) {

						continue;
					}
					const std::vector<RenderFeatureOutputSettings> outputs =
						candidate.outputs.empty() ?
							std::vector<RenderFeatureOutputSettings>{
								RenderFeatureOutputSettings{} } : candidate.outputs;
					for (const RenderFeatureOutputSettings& output : outputs) {

						const std::string item = candidate.name + " / " +
							output.name;
						const bool selected = hasPassInput &&
							passInputValue.pass == candidate.id &&
							passInputValue.output == output.name;
						if (ImGui::Selectable(item.c_str(), selected)) {
							pass.passInputs[binding.name] = {
								.pass = candidate.id,
								.output = output.name,
							};
							pass.sceneInputs.erase(binding.name);
							changed = true;
						}
					}
				}
				ImGui::EndCombo();
			}
			const auto textureOverride =
				pass.textureOverrides.find(binding.name);
			AssetID texture = textureOverride == pass.textureOverrides.end() ?
				AssetID{} : textureOverride->second;
			AssetEditSetting setting{};
			if (MyGUI::AssetReferenceField(binding.name.c_str(), texture,
				context.toolContext.assetDatabase,
				{ AssetType::Texture }, setting).valueChanged) {

				if (texture) {
					pass.textureOverrides[binding.name] = texture;
					pass.sceneInputs.erase(binding.name);
					pass.passInputs.erase(binding.name);
				} else {
					pass.textureOverrides.erase(binding.name);
				}
				changed = true;
			}
			ImGui::PopID();
		}
	}
	if (const auto* samplers = service.FindReflectionSamplers(
		pass.material, pass.materialPass)) {
		for (const ShaderResourceBinding& binding : *samplers) {

			if (binding.name.empty()) {
				continue;
			}
			ImGui::PushID(binding.name.c_str());
			if (MyGUI::CollapsingHeader(binding.name.c_str(), false)) {
				const auto found = pass.samplerOverrides.find(binding.name);
				bool overrideSampler = found != pass.samplerOverrides.end();
				if (MyGUI::Checkbox("上書き", overrideSampler)) {
					if (overrideSampler) {
						pass.samplerOverrides.emplace(binding.name,
							PipelineStaticSamplerSettings{});
					} else {
						pass.samplerOverrides.erase(binding.name);
					}
					changed = true;
				}
				if (overrideSampler) {
					PipelineStaticSamplerSettings settings =
						pass.samplerOverrides.at(binding.name);
					if (DrawSamplerSettings(settings)) {
						pass.samplerOverrides[binding.name] = settings;
						changed = true;
					}
				}
			}
			ImGui::PopID();
		}
	}
	if (changed) {
		SetDirty();
	}
}
