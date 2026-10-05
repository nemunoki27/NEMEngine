#include "MeshMaterialEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>
#include <variant>

namespace {

	// 表面方式の表示名を取得する
	const char* SurfaceModeLabel(Engine::MaterialSurfaceMode surfaceMode) {

		switch (surfaceMode) {
		case Engine::MaterialSurfaceMode::Auto:
			return "自動";
		case Engine::MaterialSurfaceMode::Opaque:
			return "不透明";
		case Engine::MaterialSurfaceMode::Masked:
			return "マスク";
		case Engine::MaterialSurfaceMode::Transparent:
			return "半透明";
		default:
			return "不明";
		}
	}

	// 編集できない値を表示する
	void DrawTextProperty(const char* label, const char* value) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}
		ImGui::TextUnformatted(value);
		Engine::MyGUI::EndPropertyRow();
	}

	// 表面方式の候補を表示する
	Engine::ValueEditResult DrawSurfaceModeCombo(const char* label, Engine::MaterialSurfaceMode& surfaceMode) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##Value", SurfaceModeLabel(surfaceMode))) {

			constexpr std::array modes{
				Engine::MaterialSurfaceMode::Auto,
				Engine::MaterialSurfaceMode::Opaque,
				Engine::MaterialSurfaceMode::Masked,
				Engine::MaterialSurfaceMode::Transparent,
			};
			for (const Engine::MaterialSurfaceMode mode : modes) {

				const bool selected = surfaceMode == mode;
				if (ImGui::Selectable(SurfaceModeLabel(mode), selected)) {
					surfaceMode = mode;
					result.valueChanged = true;
					result.editFinished = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		result.anyItemActive = ImGui::IsItemActive();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// 専用の編集欄を持つ値を判定する
	bool IsDedicatedSubMeshProperty(const Engine::ShaderConstantBufferVariable& variable) {

		return variable.semantic == Engine::MaterialParameterSemantic::AlphaClip;
	}

	// 型と保存値の一致を判定する
	bool ParamValueEqual(const Engine::MaterialParameterValue& a, const Engine::MaterialParameterValue& b) {

		if (a.value.index() != b.value.index()) {
			return false;
		}
		return std::visit(
			[&](const auto& av) {
				using T = std::decay_t<decltype(av)>;
				const T& bv = std::get<T>(b.value);
				return std::memcmp(&av, &bv, sizeof(T)) == 0;
			},
			a.value);
	}
}

//============================================================================
//	MeshMaterialEditSession classMethods
//============================================================================

Engine::ValueEditResult Engine::MeshMaterialEditSession::DrawMaterialFields(
	const EditorPanelContext& context, const MeshRendererComponent& renderer, SubMeshMaterial& subMesh) {

	ValueEditResult editResult{};

	InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
		ValueEditResult result{};
		result.valueChanged = MyGUI::Checkbox("表示", subMesh.visible);
		result.editFinished = result.valueChanged;
		return result;
	});
	InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
		AssetEditSetting setting{};
		setting.defaultAssetID =
			renderer.material ? renderer.material : DefaultMaterialSettings::GetInstance().GetMeshOrBuiltin();
		return MyGUI::AssetReferenceField(
			"マテリアル", subMesh.material, context.editorContext->assetDatabase, {AssetType::Material}, setting);
	});
	InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() { return DrawSurfaceModeCombo("表面方式", subMesh.surfaceMode); });

	DrawTextProperty("モデル判定", SurfaceModeLabel(subMesh.sourceSurfaceMode));
	const MaterialSurfaceMode resolvedMode = ResolveSubMeshSurfaceMode(context, renderer, subMesh);
	DrawTextProperty("描画方式", SurfaceModeLabel(resolvedMode));

	if (resolvedMode == MaterialSurfaceMode::Masked) {

		const AssetID materialID = subMesh.material ? subMesh.material : renderer.material;
		const MaterialAsset* material = nullptr;
		if (context.renderPipeline) {
			material = context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(materialID);
		}
		if (material && material->shaderGraph) {
			DrawTextProperty("アルファ破棄閾値", "Shader Graph出力");
			return editResult;
		}

		InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
			return MyGUI::DragFloat(
				"アルファ破棄閾値", subMesh.alphaCutoff, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f});
		});
	}
	return editResult;
}

Engine::ValueEditResult Engine::MeshMaterialEditSession::DrawBatch(
	const EditorPanelContext& context, const MeshRendererComponent& draft, std::span<SubMeshMaterial> subMeshes) {

	ValueEditResult editResult{};

	ImGui::SeparatorText("マテリアル同時編集");

	// 同時編集の表示状態を切り替える
	MyGUI::Checkbox("同時編集", batchEditSubMeshMaterials_);
	if (!batchEditSubMeshMaterials_) {
		batchOverrideAllowed_.clear();
		return editResult;
	}
	if (subMeshes.empty()) {
		return editResult;
	}

	const AssetID commonMaterial = subMeshes.front().material ? subMeshes.front().material : draft.material;
	for (const SubMeshMaterial& subMesh : subMeshes) {

		const AssetID materialID = subMesh.material ? subMesh.material : draft.material;
		if (materialID != commonMaterial) {
			ImGui::TextDisabled("サブメッシュごとにマテリアルが異なります");
			return editResult;
		}
	}

	const ShaderReflectionInfo* reflection = materialReflection_.EnsureReflection(
		context, commonMaterial, DefaultMaterialSettings::GetInstance().GetMeshOrBuiltin());
	if (!reflection) {
		ImGui::TextDisabled("マテリアルのパラメータを取得できません");
		return editResult;
	}
	MaterialParameterLayout layout{};
	layout.Build(*reflection, MaterialParameterCBuffer::kMesh);
	if (!layout.IsValid()) {
		return editResult;
	}

	// 編集確定値を全サブメッシュへ書き込む
	auto applyToAll = [&](const ShaderConstantBufferVariable& var, const MaterialParameterValue& value) {
		for (SubMeshMaterial& subMesh : subMeshes) {
			subMesh.materialInstance.Set(var.parameterID, var.name, var.semantic, value);
		}
	};

	// 混在値を確認して編集行を表示する
	auto drawVar = [&](const ShaderConstantBufferVariable& var) {
		const auto labelContextMenu = MaterialParameterEditor::MakeLabelContextMenu(var.parameterID, var.name);

		// 全SubMeshの値が一致するか調べる
		MaterialParameterValue common = materialReflection_.ResolveValue(subMeshes.front().materialInstance, var);
		bool mixed = false;
		for (size_t i = 1; i < subMeshes.size(); ++i) {
			if (!ParamValueEqual(materialReflection_.ResolveValue(subMeshes[i].materialInstance, var), common)) {
				mixed = true;
				break;
			}
		}
		const bool allowed = batchOverrideAllowed_.count(var.name) != 0;

		// 混在値は上書きを許可するまで編集を止める
		if (mixed && !allowed) {

			if (MyGUI::BeginPropertyRow(var.name.c_str())) {

				const float spacing = ImGui::GetStyle().ItemSpacing.x;
				const float buttonWidth = ImGui::CalcTextSize("上書きを許可").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				const float dashWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - buttonWidth - spacing);
				ImGui::BeginDisabled();
				ImGui::Button((std::string("-##batchdash_") + var.name).c_str(), ImVec2(dashWidth, 0.0f));
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button((std::string("上書きを許可##batch_") + var.name).c_str(), ImVec2(buttonWidth, 0.0f))) {
					batchOverrideAllowed_.insert(var.name);
				}
				MyGUI::EndPropertyRow();
			}
			return;
		}

		// 編集値を全SubMeshへ適用する
		if (MaterialParameterEditor::IsReflectedTextureParam(var, *reflection)) {

			AssetID textureID{};
			if (std::holds_alternative<AssetID>(common.value)) {
				textureID = std::get<AssetID>(common.value);
			}
			InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
				ValueEditResult result{};
				AssetEditSetting setting{};
				setting.graphicsCore = context.graphicsCore;
				auto fieldResult = MyGUI::AssetReferenceField(var.name.c_str(), textureID, context.editorContext->assetDatabase,
					{AssetType::Texture, AssetType::RenderTexture}, setting);
				if (fieldResult.valueChanged) {

					MaterialParameterValue value{};
					value.value = textureID;
					applyToAll(var, value);
					result.valueChanged = true;
					result.editFinished = true;
				}
				return result;
			});
		} else {

			MaterialParameterValue value = common;
			const Engine::FloatEditSetting floatSetting{};
			InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
				// 変更値を反映して確定時に保存を要求する
				ValueEditResult result = MaterialParameterEditor::DrawValueEdit(var, value, floatSetting);
				if (result.valueChanged) {
					applyToAll(var, value);
				}
				return result;
			});
		}
	};

	std::vector<const ShaderConstantBufferVariable*> scalarVariables;
	std::vector<const ShaderConstantBufferVariable*> textureVariables;
	CollectParameters(*reflection, layout, scalarVariables, textureVariables);

	for (const ShaderConstantBufferVariable* var : scalarVariables) {
		drawVar(*var);
	}
	// Textureを下段へまとめる
	for (const ShaderConstantBufferVariable* var : textureVariables) {
		drawVar(*var);
	}
	return editResult;
}

Engine::ValueEditResult Engine::MeshMaterialEditSession::DrawParameters(
	const EditorPanelContext& context, AssetID materialID, SubMeshMaterial& subMesh) {

	ValueEditResult editResult{};

	const ShaderReflectionInfo* reflection =
		materialReflection_.EnsureReflection(context, materialID, DefaultMaterialSettings::GetInstance().GetMeshOrBuiltin());
	if (!reflection) {
		ImGui::TextDisabled("マテリアルのパラメータを取得できません");
		return editResult;
	}

	MaterialParameterLayout layout{};
	layout.Build(*reflection, MaterialParameterCBuffer::kMesh);
	if (!layout.IsValid()) {
		return editResult;
	}

	ImGui::SeparatorText("シェーダーパラメータ");

	std::vector<const ShaderConstantBufferVariable*> scalarVariables;
	std::vector<const ShaderConstantBufferVariable*> textureVariables;
	CollectParameters(*reflection, layout, scalarVariables, textureVariables);

	for (const ShaderConstantBufferVariable* var : scalarVariables) {

		MaterialParameterValue value = materialReflection_.ResolveValue(subMesh.materialInstance, *var);
		const Engine::FloatEditSetting floatSetting{};
		InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
			// 変更値を反映して確定時に保存を要求する
			Engine::ValueEditResult result = MaterialParameterEditor::DrawValueEdit(*var, value, floatSetting);
			if (result.valueChanged) {
				subMesh.materialInstance.Set(var->parameterID, var->name, var->semantic, value);
			}
			return result;
		});
	}

	// Textureを下段へまとめる
	for (const ShaderConstantBufferVariable* var : textureVariables) {

		AssetID textureID{};
		const MaterialParameterValue value = materialReflection_.ResolveValue(subMesh.materialInstance, *var);
		if (std::holds_alternative<AssetID>(value.value)) {
			textureID = std::get<AssetID>(value.value);
		}
		InspectorDrawerCommon::DrawFieldEdit(editResult, [&]() {
			AssetEditSetting setting{};
			setting.graphicsCore = context.graphicsCore;
			const auto labelContextMenu = MaterialParameterEditor::MakeLabelContextMenu(var->parameterID, var->name);
			auto result = MyGUI::AssetReferenceField(var->name.c_str(), textureID, context.editorContext->assetDatabase,
				{AssetType::Texture, AssetType::RenderTexture}, setting);
			if (result.valueChanged) {
				MaterialParameterValue parameter{};
				parameter.value = textureID;
				subMesh.materialInstance.Set(var->parameterID, var->name, var->semantic, parameter);
			}
			return result;
		});
	}
	return editResult;
}

Engine::MaterialSurfaceMode Engine::MeshMaterialEditSession::ResolveSubMeshSurfaceMode(
	const EditorPanelContext& context, const MeshRendererComponent& renderer, const SubMeshMaterial& subMesh) {

	if (subMesh.surfaceMode != MaterialSurfaceMode::Auto) {
		return subMesh.surfaceMode;
	}

	const AssetID materialID = subMesh.material ? subMesh.material : renderer.material;
	if (materialID && context.renderPipeline) {

		const MaterialAsset* material = context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(materialID);
		if (material && material->renderState.overridesRenderer) {
			return material->renderState.surfaceMode;
		}
	}
	if (subMesh.sourceSurfaceMode != MaterialSurfaceMode::Auto) {
		return subMesh.sourceSurfaceMode;
	}
	return renderer.queue == RenderPhase::Transparent ? MaterialSurfaceMode::Transparent : MaterialSurfaceMode::Opaque;
}

void Engine::MeshMaterialEditSession::CollectParameters(const ShaderReflectionInfo& reflection,
	const MaterialParameterLayout& layout, std::vector<const ShaderConstantBufferVariable*>& scalars,
	std::vector<const ShaderConstantBufferVariable*>& textures) {

	// 専用項目と未使用の値を除外する
	for (const ShaderConstantBufferVariable& variable : layout.GetVariables()) {
		if (!variable.used || MaterialParameterEditor::IsInternalPaddingParameter(variable) ||
			IsDedicatedSubMeshProperty(variable)) {
			continue;
		}
		if (MaterialParameterEditor::IsReflectedTextureParam(variable, reflection)) {
			textures.emplace_back(&variable);
		} else {
			scalars.emplace_back(&variable);
		}
	}

	// Textureを下段へまとめて表示順を揃える
	MaterialParameterEditor::SortScalarParametersForDisplay(scalars);
	MaterialParameterEditor::SortTextureParametersForDisplay(textures);
}
