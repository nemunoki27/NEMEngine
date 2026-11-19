#include "ShaderGraphNodeInspector.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditOperations.h"
#include "ShaderGraphGenericNodeInspector.h"
#include "ShaderGraphNodePreviewUtility.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include "ShaderGraphEnumWidget.h"

#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>

// c++
#include <algorithm>
#include <cfloat>
#include <string>
#include <variant>
// imgui
#include <imgui.h>

using Engine::ShaderGraphEditOperations::DefaultValueForGraphType;

using Engine::ShaderGraphWidgets::D3D12EnumCombo;

void Engine::ShaderGraphNodeInspector::Draw(
	const EditorToolContext& context, ShaderGraphEditSession& session, std::span<const UUID> selected) {

	// 単一選択のNodeだけを編集する
	if (selected.size() != 1) {
		return;
	}

	const auto found = std::find_if(session.GetDraft().nodes.begin(), session.GetDraft().nodes.end(),
		[&](const ShaderGraphNode& node) { return node.id == selected.front(); });
	if (found == session.GetDraft().nodes.end()) {
		return;
	}

	ShaderGraphNode& node = *found;
	ImGui::SeparatorText("選択ノード");
	ImGui::TextUnformatted(GetShaderGraphNodeName(node.kind).data());
	DrawPreviewSettings(session, node);
	ShaderGraphGenericNodeInspector::Draw(session, node);
	if (node.kind != ShaderGraphNodeKind::SamplerState) {
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSelectedNode");
		session.MarkDirty(MyGUI::EnumCombo("精度", node.precision).valueChanged);
	}
	if (node.kind == ShaderGraphNodeKind::SamplerState) {

		// Sample時の補間と境界の扱いを編集する
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSamplerState");
		session.MarkDirty(D3D12EnumCombo("Filter", node.sampler.filter).valueChanged);
		session.MarkDirty(D3D12EnumCombo("Address U", node.sampler.addressU).valueChanged);
		session.MarkDirty(D3D12EnumCombo("Address V", node.sampler.addressV).valueChanged);
		session.MarkDirty(D3D12EnumCombo("Address W", node.sampler.addressW).valueChanged);
		session.MarkDirty(D3D12EnumCombo("Comparison", node.sampler.comparisonFunc).valueChanged);
		session.MarkDirty(D3D12EnumCombo("Border Color", node.sampler.borderColor).valueChanged);
		int32_t maxAnisotropy = static_cast<int32_t>(node.sampler.maxAnisotropy);
		if (MyGUI::DragInt("Anisotropy", maxAnisotropy,
				{
					.dragSpeed = 1.0f,
					.minValue = 1,
					.maxValue = 16,
				})
				.valueChanged) {
			node.sampler.maxAnisotropy = static_cast<uint32_t>(maxAnisotropy);
			session.MarkDirty();
		}
		session.MarkDirty(MyGUI::DragFloat("Mip LOD Bias", node.sampler.mipLODBias).valueChanged);
		session.MarkDirty(MyGUI::DragFloat("Min LOD", node.sampler.minLOD).valueChanged);
		session.MarkDirty(MyGUI::DragFloat("Max LOD", node.sampler.maxLOD).valueChanged);
		return;
	}

	if (node.kind == ShaderGraphNodeKind::SubGraph) {
		// 参照Graphの変更時に公開Portを作り直す
		AssetID subGraph = node.subGraph;
		if (MyGUI::AssetReferenceField("グラフ", subGraph, context.toolContext.assetDatabase, {AssetType::ShaderGraph})
				.valueChanged) {
			node.subGraph = subGraph;
			node.inputPorts.clear();
			node.outputPorts.clear();
			node.previewOutputSlot = 0;
			std::erase_if(session.GetDraft().links,
				[&](const ShaderGraphLink& link) { return link.inputNode == node.id || link.outputNode == node.id; });
			ShaderGraphAsset child;
			if (ShaderGraphAssetReferences::Load(subGraph, context.toolContext.assetDatabase, child)) {
				const ShaderGraphAssetResolver resolver = [&](AssetID id, ShaderGraphAsset& asset) {
					return ShaderGraphAssetReferences::Load(id, context.toolContext.assetDatabase, asset);
				};
				ShaderGraphAssetReferences::RefreshSubGraphPorts(node, child, resolver);
			}
			session.MarkDirty();
		}
		return;
	}
	if (node.kind != ShaderGraphNodeKind::CustomFunction) {
		return;
	}

	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphCustomFunction");
		session.MarkDirty(MyGUI::InputText("関数名", node.functionName).valueChanged);
		session.MarkDirty(MyGUI::EnumCombo("ソース", node.customFunctionSource).valueChanged);
		if (node.customFunctionSource == ShaderGraphCustomFunctionSource::File) {
			AssetID functionFile = node.functionFileAsset;
			if (MyGUI::AssetReferenceField("HLSLファイル", functionFile, context.toolContext.assetDatabase, {AssetType::Shader})
					.valueChanged) {

				node.functionFileAsset = functionFile;
				node.functionFile.clear();
				session.MarkDirty();
			}
		} else {
			TextEditSetting setting{};
			setting.multiLine = true;
			setting.size.y = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
			session.MarkDirty(MyGUI::InputText("関数本体", node.functionBody, setting).valueChanged);
		}
	}

	// Port削除後の接続番号を詰める
	const auto removePort = [&](bool input, uint32_t index) {
		std::erase_if(session.GetDraft().links, [&](const ShaderGraphLink& link) {
			return input ? (link.inputNode == node.id && link.inputSlot == index)
						 : (link.outputNode == node.id && link.outputSlot == index);
		});
		for (ShaderGraphLink& link : session.GetDraft().links) {
			if (input && link.inputNode == node.id && index < link.inputSlot) {
				--link.inputSlot;
			} else if (!input && link.outputNode == node.id && index < link.outputSlot) {
				--link.outputSlot;
			}
		}
		auto& ports = input ? node.inputPorts : node.outputPorts;
		ports.erase(ports.begin() + index);
		if (!input) {
			if (index < node.previewOutputSlot) {
				--node.previewOutputSlot;
			} else if (ports.size() <= node.previewOutputSlot) {
				node.previewOutputSlot = ports.empty() ? 0 : static_cast<uint32_t>(ports.size() - 1);
			}
		}
		session.MarkDirty();
	};
	const auto drawPorts = [&](const char* label, bool input) {
		ImGui::SeparatorText(label);
		auto& ports = input ? node.inputPorts : node.outputPorts;
		for (uint32_t index = 0; index < ports.size();) {
			ShaderGraphPort& port = ports[index];
			ImGui::PushID(static_cast<int>(index) + (input ? 0 : 1000));
			MyGUI::ScopedPropertyLabelWidth labelWidth(input ? "ShaderGraphCustomInput" : "ShaderGraphCustomOutput");
			session.MarkDirty(MyGUI::InputText("名前", port.name).valueChanged);
			const ShaderGraphValueType oldType = port.type;
			if (MyGUI::EnumCombo("型", port.type).valueChanged) {
				if (port.type == ShaderGraphValueType::Invalid || port.type == ShaderGraphValueType::Texture2D ||
					port.type == ShaderGraphValueType::SamplerState) {
					port.type = oldType;
				} else {
					port.defaultValue = DefaultValueForGraphType(port.type);
					session.MarkDirty();
				}
			}
			if (ImGui::Button("削除", ImVec2(-FLT_MIN, 0.0f))) {
				removePort(input, index);
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++index;
		}
		const char* addButtonLabel = input ? "追加##CustomFunctionInput" : "追加##CustomFunctionOutput";
		if (ImGui::Button(addButtonLabel, ImVec2(-FLT_MIN, 0.0f))) {
			const uint32_t suffix = static_cast<uint32_t>(ports.size() + 1);
			ports.emplace_back(ShaderGraphPort{
				.id = UUID::New(),
				.name = std::string(input ? "Input" : "Output") + std::to_string(suffix),
				.type = ShaderGraphValueType::Float,
				.defaultValue = DefaultValueForGraphType(ShaderGraphValueType::Float),
			});
			session.MarkDirty();
		}
	};
	drawPorts("入力", true);
	drawPorts("出力", false);
}

void Engine::ShaderGraphNodeInspector::DrawPreviewSettings(ShaderGraphEditSession& session, ShaderGraphNode& node) {

	if (!ShaderGraphNodePreviewUtility::IsPreviewableNode(node)) {
		return;
	}

	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphNodePreview");
	session.MarkDirty(MyGUI::EnumCombo("プレビュー", node.previewMode).valueChanged);
	const uint32_t outputCount = GetShaderGraphOutputCount(node);
	if (outputCount <= 1) {
		return;
	}

	std::vector<std::string> outputs;
	outputs.reserve(outputCount);
	for (uint32_t slot = 0; slot < outputCount; ++slot) {
		const std::string name(ShaderGraphNodePreviewUtility::OutputPinLabel(node, slot));
		const bool duplicate = std::any_of(outputs.begin(), outputs.end(),
			[&](const std::string& value) { return value == name; });
		outputs.emplace_back(duplicate || name.empty() ? name + " [" + std::to_string(slot + 1) + "]" : name);
	}
	const uint32_t selected = (std::min)(node.previewOutputSlot, outputCount - 1);
	std::string output = outputs[selected];
	if (MyGUI::StringCombo("プレビュー出力", output, outputs).valueChanged) {
		const auto found = std::find(outputs.begin(), outputs.end(), output);
		if (found != outputs.end()) {
			node.previewOutputSlot = static_cast<uint32_t>(std::distance(outputs.begin(), found));
			session.MarkDirty();
		}
	}
}
