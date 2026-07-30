#include "ShaderGraphEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// imgui
#include <imgui.h>
#include <imgui_node_editor.h>

// c++
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>

namespace {

	namespace ed = ax::NodeEditor;

	constexpr const char* kCreateNodePopup = "ShaderGraphCreateNode";

	uintptr_t ToNodeEditorID(Engine::UUID id) {

		return static_cast<uintptr_t>(id.value);
	}

	uintptr_t MakePinID(
		Engine::UUID node, bool input, uint32_t slot) {

		uint64_t value = node.value;
		value ^= input ?
			0x9e3779b97f4a7c15ull :
			0xc2b2ae3d27d4eb4full;
		value ^= static_cast<uint64_t>(slot + 1u) *
			0x165667b19e3779f9ull;
		return static_cast<uintptr_t>(value != 0 ? value : 1);
	}

	std::string GraphFileStem(
		const std::filesystem::path& graphPath) {

		return Engine::Algorithm::PathToUTF8(
			graphPath.stem().stem());
	}

	bool WriteTextFile(
		const std::filesystem::path& path,
		std::string_view source) {

		std::error_code ec{};
		std::filesystem::create_directories(
			path.parent_path(), ec);
		if (ec) {
			return false;
		}
		std::ofstream stream(
			path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			return false;
		}
		stream.write(
			source.data(),
			static_cast<std::streamsize>(source.size()));
		return stream.good();
	}

	nlohmann::json BuildShaderAssetJson(
		std::string_view name,
		Engine::AssetID pixelShader,
		std::string_view pixelEntry,
		std::span<const Engine::ShaderParameterMetadata>
			parameters) {

		nlohmann::json colorParameterNames =
			nlohmann::json::array();
		nlohmann::json parameterMetadata =
			nlohmann::json::array();
		for (const Engine::ShaderParameterMetadata&
			parameter : parameters) {

			if (parameter.isColor) {
				colorParameterNames.push_back(
					parameter.shaderName);
			}
			parameterMetadata.push_back({
				{ "shaderName", parameter.shaderName },
				{ "displayName", parameter.displayName },
				{ "id", Engine::ToString(
					Engine::UUID{
						parameter.id.value }) },
				{ "semantic",
					Engine::EnumAdapter<
						Engine::MaterialParameterSemantic>::
					ToString(parameter.semantic) },
				{ "isColor", parameter.isColor },
				});
		}
		nlohmann::json data{
			{ "name", name },
			{ "stages", nlohmann::json::array({
				{
					{ "stage", "VS" },
					{ "file", Engine::ToAssetReferenceJson(
						Engine::BuiltinAssets::Shaders::MeshGeometryVS) },
					{ "entry", "main" },
					{ "profile", "vs_6_0" },
				},
				{
					{ "stage", "AS" },
					{ "file", Engine::ToAssetReferenceJson(
						Engine::BuiltinAssets::Shaders::MeshGeometryAS) },
					{ "entry", "main" },
					{ "profile", "as_6_6" },
				},
				{
					{ "stage", "MS" },
					{ "file", Engine::ToAssetReferenceJson(
						Engine::BuiltinAssets::Shaders::MeshGeometryMS) },
					{ "entry", "main" },
					{ "profile", "ms_6_6" },
				},
				{
					{ "stage", "PS" },
					{ "file", Engine::ToAssetReferenceJson(pixelShader) },
					{ "entry", pixelEntry },
					{ "profile", "ps_6_6" },
				},
				}) },
			{ "colorParameters", std::move(colorParameterNames) },
			{ "parameters", std::move(parameterMetadata) },
		};
		return data;
	}

	Engine::MaterialParameterValue DefaultValueForGraphType(
		Engine::ShaderGraphValueType type) {

		Engine::MaterialParameterValue value{};
		switch (type) {
		case Engine::ShaderGraphValueType::Float:
			value.value = 0.0f;
			break;
		case Engine::ShaderGraphValueType::Float2:
			value.value = Engine::Vector2{};
			break;
		case Engine::ShaderGraphValueType::Float3:
			value.value = Engine::Vector3{};
			break;
		case Engine::ShaderGraphValueType::Float4:
			value.value = Engine::Vector4{};
			break;
		case Engine::ShaderGraphValueType::Color:
			value.value =
				Engine::Color4(1.0f, 1.0f, 1.0f, 1.0f);
			break;
		case Engine::ShaderGraphValueType::Texture2D:
			value.value = Engine::AssetID{};
			break;
		default:
			value.value = 0.0f;
			break;
		}
		return value;
	}
}

Engine::ShaderGraphEditorTool::~ShaderGraphEditorTool() {

	ResetNodeEditor();
}

void Engine::ShaderGraphEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::OpenAsset(
	AssetID assetID) {

	pendingAsset_ = assetID;
	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::DrawEditorTool(
	const EditorToolContext& context) {

	if (pendingAsset_) {
		LoadGraph(context, pendingAsset_);
		pendingAsset_ = {};
	}
	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ShaderGraphEditorTool::DrawWindow(
	const EditorToolContext& context) {

	if (!ImGui::Begin(
		"シェーダーグラフ", &openWindow_,
		ImGuiWindowFlags_MenuBar)) {

		ImGui::End();
		return;
	}

	DrawToolbar(context);
	ImGui::Separator();

	const float panelWidth =
		(std::min)(360.0f, ImGui::GetContentRegionAvail().x * 0.35f);
	if (ImGui::BeginChild(
		"ShaderGraphParameters",
		ImVec2(panelWidth, 0.0f),
		ImGuiChildFlags_Borders |
		ImGuiChildFlags_ResizeX)) {

		DrawParameterPanel(context);
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::BeginChild(
		"ShaderGraphCanvas",
		ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders)) {

		DrawGraph(context);
	}
	ImGui::EndChild();
	ImGui::End();
}

void Engine::ShaderGraphEditorTool::DrawToolbar(
	const EditorToolContext& context) {

	AssetDatabase* assetDatabase =
		context.toolContext.assetDatabase;
	AssetID selected = selectedAsset_;
	if (MyGUI::AssetReferenceField(
		"グラフ", selected, assetDatabase,
		{ AssetType::ShaderGraph }).valueChanged) {

		LoadGraph(context, selected);
	}

	MyGUI::InputText("作成先", createAssetPath_);
	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("新規作成", ImVec2(buttonWidth, 0.0f))) {
		CreateGraph(context);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!graphLoaded_);
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		CaptureNodePositions();
		if (const AssetMeta* meta =
			assetDatabase ? assetDatabase->Find(selectedAsset_) : nullptr) {

			JsonAdapter::Save(
				assetDatabase->ResolveFullPath(meta->guid),
				ToJson(graph_));
			graphDirty_ = false;
			statusMessage_ = "保存しました";
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(
		"保存してコンパイル",
		ImVec2(buttonWidth, 0.0f))) {

		SaveAndCompile(context);
	}
	ImGui::EndDisabled();

	if (!statusMessage_.empty()) {
		ImGui::TextUnformatted(statusMessage_.c_str());
	}
}

void Engine::ShaderGraphEditorTool::DrawParameterPanel(
	const EditorToolContext& context) {

	if (!graphLoaded_) {
		return;
	}

	graphDirty_ |= MyGUI::InputText(
		"名前", graph_.name).valueChanged;
	graphDirty_ |= MyGUI::EnumCombo(
		"サーフェス", graph_.surfaceMode).valueChanged;

	ImGui::SeparatorText("公開パラメータ");
	for (uint32_t index = 0;
		index < graph_.parameters.size(); ++index) {

		const ShaderGraphParameter& parameter =
			graph_.parameters[index];
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(
			parameter.name.c_str(),
			selectedParameter_ ==
				static_cast<int32_t>(index))) {

			selectedParameter_ =
				static_cast<int32_t>(index);
		}
		ImGui::PopID();
	}

	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button(
		"追加", ImVec2(buttonWidth, 0.0f))) {

		const uint32_t suffix =
			static_cast<uint32_t>(graph_.parameters.size() + 1);
		ShaderGraphParameter parameter{
			.id = UUID::New(),
			.name = "Parameter" + std::to_string(suffix),
			.type = ShaderGraphValueType::Float,
			.defaultValue =
				DefaultValueForGraphType(
					ShaderGraphValueType::Float),
		};
		graph_.parameters.emplace_back(
			std::move(parameter));
		selectedParameter_ =
			static_cast<int32_t>(
				graph_.parameters.size() - 1);
		graphDirty_ = true;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(
		selectedParameter_ < 0 ||
		static_cast<size_t>(selectedParameter_) >=
			graph_.parameters.size());
	if (ImGui::Button(
		"削除", ImVec2(buttonWidth, 0.0f))) {

		RemoveParameter(
			static_cast<uint32_t>(selectedParameter_));
	}
	ImGui::EndDisabled();

	if (0 <= selectedParameter_ &&
		static_cast<size_t>(selectedParameter_) <
			graph_.parameters.size()) {

		ImGui::SeparatorText("パラメータ設定");
		DrawParameterEditor(
			context,
			graph_.parameters[
				static_cast<size_t>(selectedParameter_)]);
	}
}

void Engine::ShaderGraphEditorTool::DrawParameterEditor(
	const EditorToolContext& context,
	ShaderGraphParameter& parameter) {

	const std::string parameterID =
		ToString(parameter.id);
	ImGui::TextDisabled(
		"ID: %s", parameterID.c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("コピー##ParameterID")) {
		ImGui::SetClipboardText(
			parameterID.c_str());
		statusMessage_ =
			"パラメータIDをコピーしました";
	}

	graphDirty_ |= MyGUI::InputText(
		"名前", parameter.name).valueChanged;

	const ShaderGraphValueType oldType = parameter.type;
	if (MyGUI::EnumCombo(
		"型", parameter.type).valueChanged) {

		if (parameter.type == ShaderGraphValueType::Invalid) {
			parameter.type = oldType;
		} else {
			parameter.defaultValue =
				DefaultValueForGraphType(parameter.type);
			for (ShaderGraphNode& node : graph_.nodes) {
				if (node.kind == ShaderGraphNodeKind::Parameter &&
					node.parameterID == parameter.id) {

					node.valueType = parameter.type;
				}
			}
			graphDirty_ = true;
		}
	}
	graphDirty_ |= MyGUI::EnumCombo(
		"Semantic", parameter.semantic).valueChanged;

	switch (parameter.type) {
	case ShaderGraphValueType::Float: {
		float value =
			std::get_if<float>(&parameter.defaultValue.value) ?
			std::get<float>(parameter.defaultValue.value) : 0.0f;
		if (MyGUI::DragFloat(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value =
			std::get_if<Vector2>(&parameter.defaultValue.value) ?
			std::get<Vector2>(parameter.defaultValue.value) : Vector2{};
		if (MyGUI::DragVector2(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value =
			std::get_if<Vector3>(&parameter.defaultValue.value) ?
			std::get<Vector3>(parameter.defaultValue.value) : Vector3{};
		if (MyGUI::DragVector3(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value =
			std::get_if<Vector4>(&parameter.defaultValue.value) ?
			std::get<Vector4>(parameter.defaultValue.value) : Vector4{};
		if (MyGUI::DragVector4(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value =
			std::get_if<Color4>(&parameter.defaultValue.value) ?
			std::get<Color4>(parameter.defaultValue.value) :
			Color4(1.0f, 1.0f, 1.0f, 1.0f);
		if (MyGUI::ColorEdit(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Texture2D: {
		AssetID value =
			std::get_if<AssetID>(&parameter.defaultValue.value) ?
			std::get<AssetID>(parameter.defaultValue.value) : AssetID{};
		AssetEditSetting setting{};
		setting.graphicsCore = context.panelContext ?
			context.panelContext->graphicsCore : nullptr;
		if (MyGUI::AssetReferenceField(
			"既定値", value,
			context.toolContext.assetDatabase,
			{ AssetType::Texture }, setting).valueChanged) {

			parameter.defaultValue.value = value;
			graphDirty_ = true;
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphEditorTool::DrawGraph(
	[[maybe_unused]] const EditorToolContext& context) {

	if (!graphLoaded_) {
		return;
	}
	if (!nodeEditor_) {
		nodeEditor_ = ed::CreateEditor();
		restoreNodePositions_ = true;
	}

	ed::SetCurrentEditor(nodeEditor_);
	ed::Begin("ShaderGraphNodeEditor");
	pinAddresses_.clear();
	for (ShaderGraphNode& node : graph_.nodes) {
		DrawNode(node);
	}
	for (const ShaderGraphLink& link : graph_.links) {
		ed::Link(
			ed::LinkId(ToNodeEditorID(link.id)),
			ed::PinId(MakePinID(
				link.outputNode, false,
				link.outputSlot)),
			ed::PinId(MakePinID(
				link.inputNode, true,
				link.inputSlot)));
	}

	if (restoreNodePositions_) {
		for (const ShaderGraphNode& node : graph_.nodes) {
			ed::SetNodePosition(
				ed::NodeId(ToNodeEditorID(node.id)),
				ImVec2(node.position.x, node.position.y));
		}
		restoreNodePositions_ = false;
	}

	if (ed::BeginCreate()) {
		ed::PinId firstPin{};
		ed::PinId secondPin{};
		if (ed::QueryNewLink(
			&firstPin, &secondPin) &&
			firstPin && secondPin) {

			const auto first =
				pinAddresses_.find(firstPin.Get());
			const auto second =
				pinAddresses_.find(secondPin.Get());
			if (first != pinAddresses_.end() &&
				second != pinAddresses_.end() &&
				first->second.input != second->second.input) {

				const PinAddress& input =
					first->second.input ?
					first->second : second->second;
				const PinAddress& output =
					first->second.input ?
					second->second : first->second;
				if (ed::AcceptNewItem()) {

					std::erase_if(
						graph_.links,
						[&](const ShaderGraphLink& link) {
							return link.inputNode == input.node &&
								link.inputSlot == input.slot;
						});
					graph_.links.emplace_back(
						ShaderGraphLink{
							.id = UUID::New(),
							.outputNode = output.node,
							.outputSlot = output.slot,
							.inputNode = input.node,
							.inputSlot = input.slot,
						});
					graphDirty_ = true;
				}
			} else {
				ed::RejectNewItem(
					ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
			}
		}
	}
	ed::EndCreate();

	if (ed::BeginDelete()) {
		ed::LinkId linkID{};
		while (ed::QueryDeletedLink(&linkID)) {
			if (ed::AcceptDeletedItem()) {
				const uint64_t id =
					static_cast<uint64_t>(linkID.Get());
				std::erase_if(
					graph_.links,
					[&](const ShaderGraphLink& link) {
						return link.id.value == id;
					});
				graphDirty_ = true;
			}
		}

		ed::NodeId nodeID{};
		while (ed::QueryDeletedNode(&nodeID)) {
			const UUID id{
				static_cast<uint64_t>(nodeID.Get())
			};
			if (id == graph_.outputNode) {
				ed::RejectDeletedItem();
			} else if (ed::AcceptDeletedItem()) {
				RemoveNode(id);
			}
		}
	}
	ed::EndDelete();

	if (ed::ShowBackgroundContextMenu()) {
		const ImVec2 position =
			ed::ScreenToCanvas(ImGui::GetMousePos());
		createNodePosition_ =
			Vector2(position.x, position.y);
		ImGui::OpenPopup(kCreateNodePopup);
	}
	ed::Suspend();
	DrawNodeCreationMenu();
	ed::Resume();
	ed::End();
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphEditorTool::DrawNode(
	ShaderGraphNode& node) {

	ed::BeginNode(
		ed::NodeId(ToNodeEditorID(node.id)));
	ImGui::PushID(
		static_cast<int>(node.id.value));
	ImGui::TextUnformatted(
		GetShaderGraphNodeName(node.kind).data());

	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(
			graph_.parameters.begin(),
			graph_.parameters.end(),
			[&](const ShaderGraphParameter& value) {
				return value.id == node.parameterID;
			});
		if (parameter != graph_.parameters.end()) {
			ImGui::TextDisabled(
				"%s", parameter->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::Constant ||
		node.kind == ShaderGraphNodeKind::TextureSample) {

		DrawNodeValue(node);
	}

	for (uint32_t slot = 0;
		slot < GetShaderGraphInputCount(node.kind);
		++slot) {

		const uintptr_t pinID =
			MakePinID(node.id, true, slot);
		pinAddresses_[pinID] =
			PinAddress{ node.id, slot, true };
		ed::BeginPin(
			ed::PinId(pinID),
			ed::PinKind::Input);
		ImGui::Text(
			"> %s",
			GetShaderGraphInputName(
				node.kind, slot).data());
		ed::EndPin();
	}
	for (uint32_t slot = 0;
		slot < GetShaderGraphOutputCount(node.kind);
		++slot) {

		const uintptr_t pinID =
			MakePinID(node.id, false, slot);
		pinAddresses_[pinID] =
			PinAddress{ node.id, slot, false };
		ed::BeginPin(
			ed::PinId(pinID),
			ed::PinKind::Output);
		ImGui::Text(
			"%s >",
			GetShaderGraphOutputName(
				node.kind, slot).data());
		ed::EndPin();
	}
	ImGui::PopID();
	ed::EndNode();
}

void Engine::ShaderGraphEditorTool::DrawNodeValue(
	ShaderGraphNode& node) {

	if (node.kind == ShaderGraphNodeKind::Constant) {
		const ShaderGraphValueType oldType =
			node.valueType;
		if (MyGUI::EnumCombo(
			"型", node.valueType).valueChanged) {

			if (node.valueType ==
				ShaderGraphValueType::Invalid ||
				node.valueType ==
				ShaderGraphValueType::Texture2D) {

				node.valueType = oldType;
			} else {
				node.value =
					DefaultValueForGraphType(
						node.valueType);
				graphDirty_ = true;
			}
		}
	}

	const ShaderGraphValueType valueType =
		node.kind ==
			ShaderGraphNodeKind::TextureSample ?
		ShaderGraphValueType::Color :
		node.valueType;
	switch (valueType) {
	case ShaderGraphValueType::Float: {
		float value =
			std::get_if<float>(&node.value.value) ?
			std::get<float>(node.value.value) : 0.0f;
		if (MyGUI::DragFloat(
			"値", value).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value =
			std::get_if<Vector2>(&node.value.value) ?
			std::get<Vector2>(node.value.value) :
			Vector2{};
		if (MyGUI::DragVector2(
			"値", value).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value =
			std::get_if<Vector3>(&node.value.value) ?
			std::get<Vector3>(node.value.value) :
			Vector3{};
		if (MyGUI::DragVector3(
			"値", value).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value =
			std::get_if<Vector4>(&node.value.value) ?
			std::get<Vector4>(node.value.value) :
			Vector4{};
		if (MyGUI::DragVector4(
			"値", value).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value =
			std::get_if<Color4>(&node.value.value) ?
			std::get<Color4>(node.value.value) :
			Color4::White();
		const char* label =
			node.kind ==
				ShaderGraphNodeKind::TextureSample ?
			"未設定時" : "値";
		if (MyGUI::ColorEdit(
			label, value).valueChanged) {

			node.value.value = value;
			graphDirty_ = true;
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphEditorTool::DrawNodeCreationMenu() {

	if (!ImGui::BeginPopup(kCreateNodePopup)) {
		return;
	}

	if (ImGui::BeginMenu("入力")) {
		if (ImGui::BeginMenu("定数")) {
			const std::array constantTypes{
				ShaderGraphValueType::Float,
				ShaderGraphValueType::Float2,
				ShaderGraphValueType::Float3,
				ShaderGraphValueType::Float4,
				ShaderGraphValueType::Color,
			};
			for (ShaderGraphValueType type :
				constantTypes) {

				if (ImGui::MenuItem(
					EnumAdapter<
						ShaderGraphValueType>::
					ToString(type))) {

					AddConstantNode(
						type,
						createNodePosition_);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("UV")) {
			AddNode(
				ShaderGraphNodeKind::UV,
				createNodePosition_);
		}
		if (ImGui::MenuItem("ワールド法線")) {
			AddNode(
				ShaderGraphNodeKind::WorldNormal,
				createNodePosition_);
		}
		if (ImGui::MenuItem("ワールド座標")) {
			AddNode(
				ShaderGraphNodeKind::WorldPosition,
				createNodePosition_);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("演算")) {
		const std::array kinds{
			ShaderGraphNodeKind::Add,
			ShaderGraphNodeKind::Multiply,
			ShaderGraphNodeKind::Lerp,
			ShaderGraphNodeKind::OneMinus,
			ShaderGraphNodeKind::Saturate,
			ShaderGraphNodeKind::NormalUnpack,
		};
		for (ShaderGraphNodeKind kind : kinds) {
			if (ImGui::MenuItem(
				GetShaderGraphNodeName(kind).data())) {

				AddNode(kind, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("テクスチャサンプル")) {
		AddNode(
			ShaderGraphNodeKind::TextureSample,
			createNodePosition_);
	}
	if (ImGui::BeginMenu("パラメータ")) {
		for (const ShaderGraphParameter& parameter :
			graph_.parameters) {

			if (ImGui::MenuItem(
				parameter.name.c_str())) {

				AddParameterNode(
					parameter.id,
					createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

bool Engine::ShaderGraphEditorTool::LoadGraph(
	const EditorToolContext& context,
	AssetID assetID) {

	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || !assetID) {
		selectedAsset_ = {};
		graphLoaded_ = false;
		ResetNodeEditor();
		return false;
	}

	const std::filesystem::path path =
		database->ResolveFullPath(assetID);
	ShaderGraphAsset loaded{};
	if (path.empty() ||
		!FromJson(JsonAdapter::Load(path, false), loaded)) {

		statusMessage_ =
			"グラフを読み込めませんでした";
		return false;
	}

	selectedAsset_ = assetID;
	graph_ = std::move(loaded);
	graphLoaded_ = true;
	graphDirty_ = false;
	selectedParameter_ = -1;
	statusMessage_.clear();
	ResetNodeEditor();
	restoreNodePositions_ = true;
	return true;
}

bool Engine::ShaderGraphEditorTool::CreateGraph(
	const EditorToolContext& context) {

	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || createAssetPath_.empty()) {
		statusMessage_ = "作成先を設定してください";
		return false;
	}

	const std::filesystem::path path =
		database->ResolveAssetPath(createAssetPath_);
	if (path.empty()) {
		statusMessage_ = "GameAssets内を指定してください";
		return false;
	}
	if (std::filesystem::exists(path)) {
		statusMessage_ = "同名のグラフが存在します";
		return false;
	}

	const std::string name = GraphFileStem(path);
	ShaderGraphAsset graph =
		CreateDefaultSurfaceShaderGraph(name);
	JsonAdapter::Save(path, ToJson(graph));

	const std::string assetPath =
		RuntimePaths::ToAssetPath(path);
	const AssetID assetID =
		database->ImportOrGet(
			assetPath, AssetType::ShaderGraph);
	if (!assetID) {
		statusMessage_ =
			"グラフをAssetDatabaseへ登録できませんでした";
		return false;
	}
	statusMessage_ = "グラフを作成しました";
	return LoadGraph(context, assetID);
}

bool Engine::ShaderGraphEditorTool::SaveAndCompile(
	const EditorToolContext& context) {

	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || !selectedAsset_) {
		return false;
	}
	const std::filesystem::path graphPath =
		database->ResolveFullPath(selectedAsset_);
	if (graphPath.empty()) {
		statusMessage_ = "グラフのパスを解決できません";
		return false;
	}

	CaptureNodePositions();
	const std::string stem = GraphFileStem(graphPath);
	const std::filesystem::path generatedRoot =
		graphPath.parent_path() /
		"GeneratedShaderGraph" /
		Algorithm::PathFromUTF8(stem);
	const std::filesystem::path surfacePath =
		generatedRoot /
		Algorithm::PathFromUTF8(
			stem + ".surface.generated.hlsli");
	const std::filesystem::path opaquePixelPath =
		generatedRoot /
		Algorithm::PathFromUTF8(
			stem + ".opaque.PS.hlsl");
	const std::filesystem::path transparentPixelPath =
		generatedRoot /
		Algorithm::PathFromUTF8(
			stem + ".transparent.PS.hlsl");

	const ShaderGraphCompileOutput output =
		ShaderGraphCompiler::Compile(
			graph_,
			Algorithm::PathToUTF8(
				surfacePath.filename()));
	if (!output.Succeeded()) {
		statusMessage_ =
			output.diagnostics.front().message;
		return false;
	}
	if (!WriteTextFile(
		surfacePath, output.surfaceHLSL) ||
		!WriteTextFile(
			opaquePixelPath, output.opaquePixelHLSL) ||
		!WriteTextFile(
			transparentPixelPath,
			output.transparentPixelHLSL)) {

		statusMessage_ =
			"生成HLSLを保存できませんでした";
		return false;
	}

	const AssetID opaquePixelID =
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(opaquePixelPath),
			AssetType::Shader);
	const AssetID transparentPixelID =
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(transparentPixelPath),
			AssetType::Shader);
	database->ImportOrGet(
		RuntimePaths::ToAssetPath(surfacePath),
		AssetType::Shader);
	if (!opaquePixelID || !transparentPixelID) {
		statusMessage_ =
			"生成HLSLをAssetDatabaseへ登録できませんでした";
		return false;
	}

	const std::filesystem::path opaqueShaderPath =
		generatedRoot /
		Algorithm::PathFromUTF8(
			stem + ".opaque.shader.json");
	const std::filesystem::path transparentShaderPath =
		generatedRoot /
		Algorithm::PathFromUTF8(
			stem + ".transparent.shader.json");
	JsonAdapter::Save(
		opaqueShaderPath,
		BuildShaderAssetJson(
			stem + "Opaque",
			opaquePixelID, "main",
			output.parameters));
	JsonAdapter::Save(
		transparentShaderPath,
		BuildShaderAssetJson(
			stem + "Transparent",
			transparentPixelID, "mainTransparent",
			output.parameters));

	graph_.generatedOpaqueShader =
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(
				opaqueShaderPath),
			AssetType::Shader);
	graph_.generatedTransparentShader =
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(
				transparentShaderPath),
			AssetType::Shader);
	if (!graph_.generatedOpaqueShader ||
		!graph_.generatedTransparentShader) {

		statusMessage_ =
			"生成Shaderを登録できませんでした";
		return false;
	}

	// 標準Mesh Passを基点にし、グラフが生成したPSだけを差し替える
	MaterialAsset material =
		CreateDefaultMeshMaterialAsset(graph_.name);
	material.renderState.overridesRenderer = true;
	material.renderState.phase =
		graph_.surfaceMode == ShaderGraphSurfaceMode::Transparent ?
		RenderPhase::Transparent : RenderPhase::Opaque;
	material.renderState.blendMode = BlendMode::Normal;
	if (MaterialPassBinding* drawPass =
		FindPass(material, MaterialPassKind::Draw)) {

		drawPass->shaderOverride =
			graph_.generatedOpaqueShader;
	}
	if (MaterialPassBinding* transparentPass =
		FindPass(material, MaterialPassKind::Transparent)) {

		transparentPass->shaderOverride =
			graph_.generatedTransparentShader;
	}
	for (const ShaderGraphParameter& parameter :
		graph_.parameters) {

		material.parameters.Set(
			MaterialParameterID::FromUUID(parameter.id),
			parameter.name, parameter.semantic,
			parameter.defaultValue);
	}

	const std::filesystem::path materialPath =
		generatedRoot /
		Algorithm::PathFromUTF8(
			stem + ".material.json");
	JsonAdapter::Save(
		materialPath, ToJson(material));
	graph_.generatedMaterial =
		database->ImportOrGet(
			RuntimePaths::ToAssetPath(materialPath),
			AssetType::Material);
	if (!graph_.generatedMaterial) {
		statusMessage_ =
			"生成Materialを登録できませんでした";
		return false;
	}

	JsonAdapter::Save(graphPath, ToJson(graph_));
	graphDirty_ = false;
	if (context.panelContext &&
		context.panelContext->renderPipeline) {

		context.panelContext->renderPipeline->ReloadAsset(
			*database, graph_.generatedOpaqueShader);
		context.panelContext->renderPipeline->ReloadAsset(
			*database, graph_.generatedTransparentShader);
		context.panelContext->renderPipeline->ReloadAsset(
			*database, graph_.generatedMaterial);
	}
	statusMessage_ = "コンパイルしました";
	return true;
}

void Engine::ShaderGraphEditorTool::CaptureNodePositions() {

	if (!nodeEditor_) {
		return;
	}
	ed::SetCurrentEditor(nodeEditor_);
	for (ShaderGraphNode& node : graph_.nodes) {
		const ImVec2 position =
			ed::GetNodePosition(
				ed::NodeId(ToNodeEditorID(node.id)));
		node.position =
			Vector2(position.x, position.y);
	}
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphEditorTool::ResetNodeEditor() {

	if (nodeEditor_) {
		ed::DestroyEditor(nodeEditor_);
		nodeEditor_ = nullptr;
	}
	pinAddresses_.clear();
}

void Engine::ShaderGraphEditorTool::RemoveNode(
	UUID nodeID) {

	std::erase_if(
		graph_.nodes,
		[&](const ShaderGraphNode& node) {
			return node.id == nodeID;
		});
	std::erase_if(
		graph_.links,
		[&](const ShaderGraphLink& link) {
			return link.inputNode == nodeID ||
				link.outputNode == nodeID;
		});
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::RemoveParameter(
	uint32_t index) {

	if (graph_.parameters.size() <= index) {
		return;
	}
	const UUID parameterID =
		graph_.parameters[index].id;
	std::vector<UUID> nodes;
	for (const ShaderGraphNode& node : graph_.nodes) {
		if (node.kind == ShaderGraphNodeKind::Parameter &&
			node.parameterID == parameterID) {

			nodes.emplace_back(node.id);
		}
	}
	for (UUID nodeID : nodes) {
		RemoveNode(nodeID);
	}
	graph_.parameters.erase(
		graph_.parameters.begin() + index);
	selectedParameter_ = -1;
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::AddNode(
	ShaderGraphNodeKind kind, Vector2 position) {

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = kind,
		.position = position,
	};
	if (kind == ShaderGraphNodeKind::Constant) {
		node.value =
			DefaultValueForGraphType(node.valueType);
	} else if (
		kind ==
		ShaderGraphNodeKind::TextureSample) {

		node.value.value = Color4::White();
	}
	graph_.nodes.emplace_back(std::move(node));
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::AddConstantNode(
	ShaderGraphValueType type,
	Vector2 position) {

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Constant,
		.valueType = type,
		.value = DefaultValueForGraphType(type),
		.position = position,
	};
	graph_.nodes.emplace_back(std::move(node));
	graphDirty_ = true;
}

void Engine::ShaderGraphEditorTool::AddParameterNode(
	UUID parameterID, Vector2 position) {

	const auto parameter = std::find_if(
		graph_.parameters.begin(),
		graph_.parameters.end(),
		[&](const ShaderGraphParameter& value) {
			return value.id == parameterID;
		});
	if (parameter == graph_.parameters.end()) {
		return;
	}
	graph_.nodes.emplace_back(ShaderGraphNode{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Parameter,
		.parameterID = parameterID,
		.valueType = parameter->type,
		.position = position,
		});
	graphDirty_ = true;
}
