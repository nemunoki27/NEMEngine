#include "ShaderGraphNodeInspector.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditOperations.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include "ShaderGraphEnumWidget.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

// c++
#include <algorithm>
#include <cfloat>
#include <filesystem>
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
	if (node.kind != ShaderGraphNodeKind::SamplerState) {
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSelectedNode");
		session.MarkDirty(MyGUI::EnumCombo("精度", node.precision).valueChanged);
	}
	if (node.kind == ShaderGraphNodeKind::SamplerState) {

		// Sample時の補間と境界の扱いを編集する
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSamplerState");
		session.MarkDirty(D3D12EnumCombo("フィルタ", node.sampler.filter).valueChanged);
		session.MarkDirty(D3D12EnumCombo("アドレスU", node.sampler.addressU).valueChanged);
		session.MarkDirty(D3D12EnumCombo("アドレスV", node.sampler.addressV).valueChanged);
		session.MarkDirty(D3D12EnumCombo("アドレスW", node.sampler.addressW).valueChanged);
		session.MarkDirty(D3D12EnumCombo("比較関数", node.sampler.comparisonFunc).valueChanged);
		session.MarkDirty(D3D12EnumCombo("境界色", node.sampler.borderColor).valueChanged);
		int32_t maxAnisotropy = static_cast<int32_t>(node.sampler.maxAnisotropy);
		if (MyGUI::DragInt("異方性", maxAnisotropy,
				{
					.dragSpeed = 1.0f,
					.minValue = 1,
					.maxValue = 16,
				})
				.valueChanged) {
			node.sampler.maxAnisotropy = static_cast<uint32_t>(maxAnisotropy);
			session.MarkDirty();
		}
		session.MarkDirty(MyGUI::DragFloat("Mip LODバイアス", node.sampler.mipLODBias).valueChanged);
		session.MarkDirty(MyGUI::DragFloat("最小LOD", node.sampler.minLOD).valueChanged);
		session.MarkDirty(MyGUI::DragFloat("最大LOD", node.sampler.maxLOD).valueChanged);
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
			std::erase_if(session.GetDraft().links,
				[&](const ShaderGraphLink& link) { return link.inputNode == node.id || link.outputNode == node.id; });
			AssetDatabase* database = context.toolContext.assetDatabase;
			ShaderGraphAsset child{};
			const std::filesystem::path childPath =
				database && subGraph ? database->ResolveFullPath(subGraph) : std::filesystem::path{};
			if (!childPath.empty() && FromJson(JsonAdapter::Load(childPath, true), child)) {
				for (const ShaderGraphParameter& parameter : child.parameters) {
					if (!parameter.exposed) {
						continue;
					}
					node.inputPorts.emplace_back(ShaderGraphPort{
						.id = UUID::New(),
						.name = parameter.name,
						.type = parameter.type,
						.defaultValue = parameter.defaultValue,
					});
				}
				const auto output = std::find_if(child.nodes.begin(), child.nodes.end(),
					[&](const ShaderGraphNode& value) { return value.id == child.outputNode; });
				if (output != child.nodes.end()) {
					const ShaderGraphNodeDescriptor* descriptor = ShaderGraphNodeRegistry::Find(output->kind);
					for (uint32_t slot = 0; slot < GetShaderGraphInputCount(*output); ++slot) {
						ShaderGraphValueType type = ShaderGraphValueType::Float;
						if (!output->inputPorts.empty()) {
							type = output->inputPorts[slot].type;
						} else if (descriptor && slot < descriptor->inputs.size()) {
							type = descriptor->inputs[slot].type;
						}
						node.outputPorts.emplace_back(ShaderGraphPort{
							.id = UUID::New(),
							.name = std::string(GetShaderGraphInputName(*output, slot)),
							.type = type,
							.defaultValue = DefaultValueForGraphType(type),
						});
					}
				}
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
