#include "ShaderGraphEditorTool.h"
#include "ShaderGraphNodePreviewUtility.h"
#include "ShaderGraphPublication.h"
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSettingsImporter.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

// c++
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_node_editor.h>

using namespace Engine::ShaderGraphAppearance;
using namespace Engine::ShaderGraphNodePreviewUtility;
using namespace Engine::ShaderGraphPublication;

namespace {

	namespace ed = ax::NodeEditor;

	constexpr const char* kCreateNodePopup = "ShaderGraphCreateNode";
	constexpr const char* kNodeContextPopup = "ShaderGraphNodeContext";
	constexpr const char* kNodeValuePopup = "ShaderGraphNodeValue";
	constexpr float kNodePinColumnGap = 32.0f;
	constexpr float kGroupNameEditWidth = 200.0f;
	constexpr float kGroupHorizontalPadding = 40.0f;
	constexpr float kGroupTopPadding = 56.0f;
	constexpr float kGroupBottomPadding = 40.0f;
	constexpr float kDuplicateOffset = 32.0f;
	constexpr float kDuplicateGroupSpacing = 48.0f;

	template <typename T>
	Engine::ValueEditResult D3D12EnumCombo(
		const char* label, T& currentValue) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(width <= 1.0f ? 1.0f : width);
		int32_t currentIndex = static_cast<int32_t>(
			Engine::EnumAdapter<T>::GetIndex(currentValue));
		const auto itemGetter = [](
			void*, int32_t index) -> const char* {

				constexpr auto names = magic_enum::enum_names<T>();
				constexpr std::string_view typeName =
					magic_enum::enum_type_name<T>();
				constexpr size_t separator = typeName.rfind('_');
				constexpr std::string_view prefix =
					separator == std::string_view::npos ?
					std::string_view{} :
					typeName.substr(0, separator + 1);
				if (index < 0 || names.size() <= static_cast<size_t>(index)) {
					return "";
				}
				std::string_view name = names[static_cast<size_t>(index)];
				if (!prefix.empty() && name.starts_with(prefix)) {
					name.remove_prefix(prefix.size());
				}
				return name.data();
			};
		result.valueChanged = ImGui::Combo(
			"##Value", &currentIndex, itemGetter, nullptr,
			static_cast<int32_t>(Engine::EnumAdapter<T>::GetEnumCount()));
		if (result.valueChanged) {
			currentValue = Engine::EnumAdapter<T>::GetValue(
				static_cast<uint32_t>(currentIndex));
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged ||
			ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	Engine::FloatEditSetting AppearanceFloatSetting(
		float minValue, float maxValue,
		float dragSpeed = 0.1f) {

		return Engine::FloatEditSetting{
			.dragSpeed = dragSpeed,
			.minValue = minValue,
			.maxValue = maxValue,
		};
	}

	Engine::PropertyRowSetting NodeValueRowSetting(
		const char* label, float rowWidth) {

		return Engine::PropertyRowSetting{
			.labelWidth =
				ImGui::CalcTextSize(label).x + 4.0f,
			.rowWidth = rowWidth,
		};
	}

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

	bool WriteTextFile(
		const std::filesystem::path& path,
		std::string_view source) {

		std::error_code ec{};
		std::filesystem::create_directories(
			path.parent_path(), ec);
		if (ec) {
			return false;
		}
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
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
				{ "isTexture", parameter.isTexture },
				});
		}
		nlohmann::json data{
			{ "name", name },
			{ "stages", nlohmann::json::array({
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

	Engine::MaterialAsset CreateShaderGraphMaterial(
		std::string_view name,
		Engine::ShaderGraphTarget target) {

		using namespace Engine;

		if (target == ShaderGraphTarget::Mesh) {
			return CreateDefaultMeshMaterialAsset(name);
		}

		MaterialAsset material{};
		material.name =
			name.empty() ? "NewMaterial" : std::string(name);
		material.domain =
			target == ShaderGraphTarget::Sprite ||
			target == ShaderGraphTarget::Text ||
			target == ShaderGraphTarget::Primitive2D ?
			MaterialDomain::UI : MaterialDomain::Surface;
		material.usage =
			target == ShaderGraphTarget::Sprite ? MaterialUsage::Sprite :
			(target == ShaderGraphTarget::Text ? MaterialUsage::Text :
				((target == ShaderGraphTarget::Particle ||
					target == ShaderGraphTarget::Trail) ?
					MaterialUsage::Particle : MaterialUsage::Generic));

		auto addPass = [&](MaterialPassKind passKind,
			AssetID pipeline,
			PipelineVariantKind variant) {

				material.passes.emplace_back(MaterialPassBinding{
					.passKind = passKind,
					.pipeline = pipeline,
					.preferredVariant = variant,
					});
			};

		switch (target) {
		case ShaderGraphTarget::Primitive3D:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultPrimitive,
				PipelineVariantKind::GraphicsMesh);
			addPass(
				MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultPrimitiveTransparent,
				PipelineVariantKind::GraphicsMesh);
			break;
		case ShaderGraphTarget::Sprite:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultSprite,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Text:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultText,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Primitive2D:
			addPass(
				MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultPrimitive2D,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Particle:
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultParticle,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Trail:
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::ParticleTrail,
				PipelineVariantKind::GraphicsMesh);
			break;
		case ShaderGraphTarget::Mesh:
			break;
		}
		return material;
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
		case Engine::ShaderGraphValueType::Boolean:
			value.value = false;
			break;
		case Engine::ShaderGraphValueType::Integer:
			value.value = int32_t{};
			break;
		default:
			value.value = 0.0f;
			break;
		}
		return value;
	}

}

//============================================================================
//	ShaderGraphEditorTool::PreviewState structure
//============================================================================

Engine::ShaderGraphEditorTool::ShaderGraphEditorTool() {

	RestoreDefaultAppearance();
	LoadAppearanceSettings();
	nodePreviews_.Init();
}

Engine::ShaderGraphEditorTool::~ShaderGraphEditorTool() {

	nodePreviews_.ClearNodePreviews();
	ResetNodeEditor();
}

void Engine::ShaderGraphEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::OpenAsset(AssetID assetID) {

	pendingAsset_ = assetID;
	openWindow_ = true;
}

void Engine::ShaderGraphEditorTool::DrawEditorTool(const EditorToolContext& context) {

	commandPanelFocused_ = false;
	if (pendingAsset_) {
		RestorePreviewMaterial(context);
		LoadGraph(context, pendingAsset_);
		pendingAsset_ = {};
	}
	if (openWindow_) {
		DrawWindow(context);
	}
	if (!openWindow_) {
		RestorePreviewMaterial(context);
		return;
	}
	UpdateMaterialPreview(context);
}

void Engine::ShaderGraphEditorTool::DrawWindow(const EditorToolContext& context) {

	const bool visible = ImGui::Begin(
		"シェーダーグラフ", &openWindow_,
		ImGuiWindowFlags_MenuBar);
	commandPanelFocused_ =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!visible) {

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
	if (editSession_.IsLoaded() &&
		!ImGui::IsAnyItemActive() &&
		!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {

		CommitGraphHistory();
	}
	ImGui::End();
}

void Engine::ShaderGraphEditorTool::DrawToolbar(const EditorToolContext& context) {

	AssetDatabase* assetDatabase =
		context.toolContext.assetDatabase;
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphToolbar");
	AssetID selected = editSession_.GetAssetID();
	if (MyGUI::AssetReferenceField(
		"グラフ", selected, assetDatabase,
		{ AssetType::ShaderGraph }).valueChanged) {

		RestorePreviewMaterial(context);
		LoadGraph(context, selected);
	}

	AssetID importSource{};
	AssetEditSetting importSetting{};
	importSetting.allowDelete = false;
	ImGui::BeginDisabled(!editSession_.IsLoaded());
	if (MyGUI::AssetReferenceField("設定をインポート", importSource, assetDatabase,
		{ AssetType::Material, AssetType::ShaderGraph }, importSetting).valueChanged) {
		ImportGraphSettings(context, importSource);
	}
	ImGui::EndDisabled();

	MyGUI::EnumCombo("作成種類", createDomain_);
	if (createDomain_ == ShaderGraphDomain::Surface) {
		MyGUI::EnumCombo("作成対象", createTarget_);
	}
	MyGUI::InputText("作成先", createAssetPath_);
	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("新規作成", ImVec2(buttonWidth, 0.0f))) {
		CreateGraph(context);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!editSession_.IsLoaded());
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		CaptureNodePositions();
		editSession_.Save(context);
	}
	ImGui::SameLine();
	if (ImGui::Button(
		"保存してコンパイル",
		ImVec2(buttonWidth, 0.0f))) {

		SaveAndCompile(context);
	}
	ImGui::EndDisabled();

	const float historyButtonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	ImGui::BeginDisabled(!editSession_.IsLoaded() || !editSession_.CanUndo());
	if (ImGui::Button(
		"元に戻す", ImVec2(historyButtonWidth, 0.0f))) {
		UndoGraph();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!editSession_.IsLoaded() || !editSession_.CanRedo());
	if (ImGui::Button(
		"やり直す", ImVec2(historyButtonWidth, 0.0f))) {
		RedoGraph();
	}
	ImGui::EndDisabled();

	if (!editSession_.GetStatusMessage().empty()) {
		ImGui::TextUnformatted(editSession_.GetStatusMessage().c_str());
	} else {
		ImGui::Dummy(ImVec2(
			0.0f, ImGui::GetTextLineHeight()));
	}
}

void Engine::ShaderGraphEditorTool::DrawParameterPanel(const EditorToolContext& context) {

	DrawAppearancePanel();
	if (!editSession_.IsLoaded()) {
		return;
	}

	ImGui::Separator();
	DrawGraphSettings(context);

	if (editSession_.GetDraft().domain == ShaderGraphDomain::Surface) {
		DrawPreviewSetting(context);
	}

	ImGui::SeparatorText("公開パラメータ");
	for (uint32_t index = 0;
		index < editSession_.GetDraft().parameters.size(); ++index) {

		const ShaderGraphParameter& parameter =
			editSession_.GetDraft().parameters[index];
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
			static_cast<uint32_t>(editSession_.GetDraft().parameters.size() + 1);
		ShaderGraphParameter parameter{
			.id = UUID::New(),
			.name = "Parameter" + std::to_string(suffix),
			.type = ShaderGraphValueType::Float,
			.defaultValue =
				DefaultValueForGraphType(
					ShaderGraphValueType::Float),
		};
		editSession_.GetDraft().parameters.emplace_back(
			std::move(parameter));
		selectedParameter_ =
			static_cast<int32_t>(
				editSession_.GetDraft().parameters.size() - 1);
		editSession_.MarkDirty();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(
		selectedParameter_ < 0 ||
		static_cast<size_t>(selectedParameter_) >=
		editSession_.GetDraft().parameters.size());
	if (ImGui::Button(
		"削除", ImVec2(buttonWidth, 0.0f))) {

		RemoveParameter(
			static_cast<uint32_t>(selectedParameter_));
	}
	ImGui::EndDisabled();

	if (0 <= selectedParameter_ &&
		static_cast<size_t>(selectedParameter_) <
		editSession_.GetDraft().parameters.size()) {

		ImGui::SeparatorText("パラメータ設定");
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphParameterSettings");
		DrawParameterEditor(
			context,
			editSession_.GetDraft().parameters[
				static_cast<size_t>(selectedParameter_)]);
	}

	DrawKeywordEditor();
	DrawSelectedNodeEditor(context);
	DrawDiagnostics();
}

void Engine::ShaderGraphEditorTool::DrawDiagnostics() {

	if (editSession_.GetDiagnostics().empty()) {
		return;
	}
	ImGui::SeparatorText("コンパイル診断");
	for (uint32_t index = 0;
		index < editSession_.GetDiagnostics().size(); ++index) {

		const ShaderGraphDiagnostic& diagnostic =
			editSession_.GetDiagnostics()[index];
		const char* prefix =
			diagnostic.severity == ShaderGraphDiagnosticSeverity::Error ?
			"エラー" :
			(diagnostic.severity == ShaderGraphDiagnosticSeverity::Warning ?
				"警告" : "情報");
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(
			(std::string(prefix) + ": " + diagnostic.message).c_str())) {

			if (nodeEditor_ && diagnostic.node) {
				ed::SetCurrentEditor(nodeEditor_);
				ed::ClearSelection();
				ed::SelectNode(ed::NodeId(
					ToNodeEditorID(diagnostic.node)));
				ed::NavigateToSelection(true);
				ed::SetCurrentEditor(nullptr);
			}
		}
		ImGui::PopID();
	}
}

void Engine::ShaderGraphEditorTool::DrawGraphSettings(const EditorToolContext& context) {

	if (!MyGUI::CollapsingHeader("グラフ設定", true)) {
		return;
	}

	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSettings");
	editSession_.MarkDirty(MyGUI::InputText(
		"名前", editSession_.GetDraft().name).valueChanged);
	ImGui::Text("種類: %s",
		EnumAdapter<ShaderGraphDomain>::ToString(editSession_.GetDraft().domain));
	editSession_.MarkDirty(MyGUI::EnumCombo(
		"既定精度", editSession_.GetDraft().defaultPrecision).valueChanged);
	if (editSession_.GetDraft().domain != ShaderGraphDomain::Surface) {
		return;
	}

	const ShaderGraphTarget oldTarget = editSession_.GetDraft().target;
	if (MyGUI::EnumCombo(
		"描画対象", editSession_.GetDraft().target).valueChanged) {

		RestorePreviewMaterial(context);
		const bool is3D =
			IsShaderGraph3DTarget(editSession_.GetDraft().target);
		for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {
			if (node.id == editSession_.GetDraft().outputNode) {
				node.kind = is3D ?
					ShaderGraphNodeKind::SurfaceOutput :
					ShaderGraphNodeKind::UnlitOutput;
				break;
			}
		}
		const uint32_t inputCount = is3D ? 8u : 3u;
		std::erase_if(
			editSession_.GetDraft().links,
			[&](const ShaderGraphLink& link) {
				return link.inputNode == editSession_.GetDraft().outputNode &&
					inputCount <= link.inputSlot;
			});
		if (oldTarget != editSession_.GetDraft().target) {
			if (!SupportsShaderGraphVertexOutput(
				editSession_.GetDraft().target) &&
				editSession_.GetDraft().vertexOutputNode) {

				RemoveNode(editSession_.GetDraft().vertexOutputNode);
				editSession_.GetDraft().vertexOutputNode = UUID{};
			}
			editSession_.MarkDirty();
			ResetNodeEditor();
			restoreNodePositions_ = true;
		}
	}
	editSession_.MarkDirty(MyGUI::EnumCombo(
		"サーフェス", editSession_.GetDraft().surfaceMode).valueChanged);
	editSession_.MarkDirty(MyGUI::EnumCombo(
		"ブレンド", editSession_.GetDraft().renderState.blendMode).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox(
		"両面描画", editSession_.GetDraft().renderState.twoSided));
	editSession_.MarkDirty(D3D12EnumCombo(
		"塗りモード", editSession_.GetDraft().renderState.fillMode).valueChanged);
	editSession_.MarkDirty(D3D12EnumCombo(
		"カリング", editSession_.GetDraft().renderState.cullMode).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox(
		"前面反時計回り", editSession_.GetDraft().renderState.frontCounterClockwise));
	editSession_.MarkDirty(MyGUI::Checkbox(
		"深度クリップ", editSession_.GetDraft().renderState.depthClipEnable));
	editSession_.MarkDirty(MyGUI::Checkbox(
		"深度書き込み", editSession_.GetDraft().renderState.depthWrite));
	editSession_.MarkDirty(MyGUI::Checkbox(
		"深度テスト", editSession_.GetDraft().renderState.depthTest));
	editSession_.MarkDirty(D3D12EnumCombo(
		"深度比較", editSession_.GetDraft().renderState.depthFunc).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox(
		"ステンシル", editSession_.GetDraft().renderState.stencilEnable));
	editSession_.MarkDirty(MyGUI::Checkbox(
		"アルファクリップ", editSession_.GetDraft().renderState.alphaClipping));
	if (IsShaderGraph3DTarget(editSession_.GetDraft().target)) {
		editSession_.MarkDirty(MyGUI::Checkbox(
			"影を落とす", editSession_.GetDraft().renderState.castShadows));
		editSession_.MarkDirty(MyGUI::Checkbox(
			"影を受ける", editSession_.GetDraft().renderState.receiveShadows));
	}

}

void Engine::ShaderGraphEditorTool::DrawKeywordEditor() {

	ImGui::SeparatorText("キーワード");
	for (uint32_t index = 0; index < editSession_.GetDraft().keywords.size(); ++index) {
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(
			editSession_.GetDraft().keywords[index].name.c_str(),
			selectedKeyword_ == static_cast<int32_t>(index))) {
			selectedKeyword_ = static_cast<int32_t>(index);
		}
		ImGui::PopID();
	}

	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x -
			ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("追加##Keyword", ImVec2(buttonWidth, 0.0f))) {
		const uint32_t suffix =
			static_cast<uint32_t>(editSession_.GetDraft().keywords.size() + 1);
		editSession_.GetDraft().keywords.emplace_back(ShaderGraphKeyword{
			.id = UUID::New(),
			.name = "Keyword" + std::to_string(suffix),
			.referenceName = "KEYWORD_" + std::to_string(suffix),
			});
		selectedKeyword_ =
			static_cast<int32_t>(editSession_.GetDraft().keywords.size() - 1);
		editSession_.MarkDirty();
	}
	ImGui::SameLine();
	const bool validSelection = selectedKeyword_ >= 0 &&
		static_cast<size_t>(selectedKeyword_) < editSession_.GetDraft().keywords.size();
	ImGui::BeginDisabled(!validSelection);
	if (ImGui::Button("削除##Keyword", ImVec2(buttonWidth, 0.0f))) {
		const UUID keywordID = editSession_.GetDraft().keywords[
			static_cast<size_t>(selectedKeyword_)].id;
		std::erase_if(editSession_.GetDraft().nodes,
			[&](const ShaderGraphNode& node) {
				return node.kind == ShaderGraphNodeKind::Keyword &&
					node.keywordID == keywordID;
			});
		std::erase_if(editSession_.GetDraft().links,
			[&](const ShaderGraphLink& link) {
				return std::none_of(
					editSession_.GetDraft().nodes.begin(), editSession_.GetDraft().nodes.end(),
					[&](const ShaderGraphNode& node) {
						return node.id == link.outputNode;
					});
			});
		editSession_.GetDraft().keywords.erase(
			editSession_.GetDraft().keywords.begin() + selectedKeyword_);
		selectedKeyword_ = -1;
		editSession_.MarkDirty();
	}
	ImGui::EndDisabled();
	if (!validSelection || selectedKeyword_ < 0) {
		return;
	}

	ShaderGraphKeyword& keyword = editSession_.GetDraft().keywords[
		static_cast<size_t>(selectedKeyword_)];
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphKeywordSettings");
	editSession_.MarkDirty(MyGUI::InputText(
		"名前", keyword.name).valueChanged);
	editSession_.MarkDirty(MyGUI::InputText(
		"参照名", keyword.referenceName).valueChanged);
	if (MyGUI::EnumCombo("型", keyword.type).valueChanged) {
		keyword.defaultIndex = 0;
		if (keyword.type == ShaderGraphKeywordType::Boolean) {
			keyword.entries.clear();
		}
		editSession_.MarkDirty();
	}
	editSession_.MarkDirty(MyGUI::Checkbox(
		"実行時切り替え", keyword.runtimeToggle));
	if (ImGui::BeginItemTooltip()) {
		ImGui::TextUnformatted(keyword.runtimeToggle ?
			"Material Instanceから値を変更する動的分岐" :
			"既定値をHLSLへ埋め込み、保存時に再コンパイル");
		ImGui::EndTooltip();
	}
	if (keyword.type == ShaderGraphKeywordType::Boolean) {
		bool defaultValue = keyword.defaultIndex != 0;
		if (MyGUI::Checkbox("既定値", defaultValue)) {
			keyword.defaultIndex = defaultValue ? 1u : 0u;
			editSession_.MarkDirty();
		}
		return;
	}

	int32_t defaultIndex = static_cast<int32_t>(keyword.defaultIndex);
	if (MyGUI::DragInt("既定値", defaultIndex, {
		.dragSpeed = 1.0f,
		.minValue = 0,
		.maxValue = (std::max)(
			static_cast<int32_t>(keyword.entries.size()) - 1, 0),
		}).valueChanged) {
		keyword.defaultIndex = static_cast<uint32_t>(defaultIndex);
		editSession_.MarkDirty();
	}
	for (uint32_t index = 0; index < keyword.entries.size();) {
		ImGui::PushID(static_cast<int>(index));
		const std::string label = "値 " + std::to_string(index);
		editSession_.MarkDirty(MyGUI::InputText(
			label.c_str(), keyword.entries[index]).valueChanged);
		ImGui::SameLine();
		if (ImGui::SmallButton("削除")) {
			keyword.entries.erase(keyword.entries.begin() + index);
			keyword.defaultIndex = (std::min)(
				keyword.defaultIndex,
				keyword.entries.empty() ? 0u :
				static_cast<uint32_t>(keyword.entries.size() - 1));
			editSession_.MarkDirty();
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++index;
	}
	if (ImGui::Button("列挙値を追加", ImVec2(-FLT_MIN, 0.0f))) {
		keyword.entries.emplace_back(
			"Value" + std::to_string(keyword.entries.size()));
		editSession_.MarkDirty();
	}
}

void Engine::ShaderGraphEditorTool::DrawSelectedNodeEditor(const EditorToolContext& context) {

	if (!nodeEditor_) {
		return;
	}
	ed::SetCurrentEditor(nodeEditor_);
	const std::vector<UUID> selected = GetSelectedGraphNodes();
	ed::SetCurrentEditor(nullptr);
	if (selected.size() != 1) {
		return;
	}

	const auto found = std::find_if(
		editSession_.GetDraft().nodes.begin(), editSession_.GetDraft().nodes.end(),
		[&](const ShaderGraphNode& node) {
			return node.id == selected.front();
		});
	if (found == editSession_.GetDraft().nodes.end()) {
		return;
	}

	ShaderGraphNode& node = *found;
	ImGui::SeparatorText("選択ノード");
	ImGui::TextUnformatted(GetShaderGraphNodeName(node.kind).data());
	if (node.kind != ShaderGraphNodeKind::SamplerState) {
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSelectedNode");
		editSession_.MarkDirty(MyGUI::EnumCombo(
			"精度", node.precision).valueChanged);
	}
	if (node.kind == ShaderGraphNodeKind::SamplerState) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSamplerState");
		editSession_.MarkDirty(D3D12EnumCombo(
			"フィルタ", node.sampler.filter).valueChanged);
		editSession_.MarkDirty(D3D12EnumCombo(
			"アドレスU", node.sampler.addressU).valueChanged);
		editSession_.MarkDirty(D3D12EnumCombo(
			"アドレスV", node.sampler.addressV).valueChanged);
		editSession_.MarkDirty(D3D12EnumCombo(
			"アドレスW", node.sampler.addressW).valueChanged);
		editSession_.MarkDirty(D3D12EnumCombo(
			"比較関数", node.sampler.comparisonFunc).valueChanged);
		editSession_.MarkDirty(D3D12EnumCombo(
			"境界色", node.sampler.borderColor).valueChanged);
		int32_t maxAnisotropy = static_cast<int32_t>(
			node.sampler.maxAnisotropy);
		if (MyGUI::DragInt("異方性", maxAnisotropy, {
			.dragSpeed = 1.0f,
			.minValue = 1,
			.maxValue = 16,
			}).valueChanged) {
			node.sampler.maxAnisotropy =
				static_cast<uint32_t>(maxAnisotropy);
			editSession_.MarkDirty();
		}
		editSession_.MarkDirty(MyGUI::DragFloat(
			"Mip LODバイアス", node.sampler.mipLODBias).valueChanged);
		editSession_.MarkDirty(MyGUI::DragFloat(
			"最小LOD", node.sampler.minLOD).valueChanged);
		editSession_.MarkDirty(MyGUI::DragFloat(
			"最大LOD", node.sampler.maxLOD).valueChanged);
		return;
	}

	if (node.kind == ShaderGraphNodeKind::SubGraph) {
		AssetID subGraph = node.subGraph;
		if (MyGUI::AssetReferenceField(
			"グラフ", subGraph,
			context.toolContext.assetDatabase,
			{ AssetType::ShaderGraph }).valueChanged) {
			node.subGraph = subGraph;
			node.inputPorts.clear();
			node.outputPorts.clear();
			std::erase_if(editSession_.GetDraft().links,
				[&](const ShaderGraphLink& link) {
					return link.inputNode == node.id ||
						link.outputNode == node.id;
				});
			AssetDatabase* database =
				context.toolContext.assetDatabase;
			ShaderGraphAsset child{};
			const std::filesystem::path childPath =
				database && subGraph ?
				database->ResolveFullPath(subGraph) :
				std::filesystem::path{};
			if (!childPath.empty() &&
				FromJson(JsonAdapter::Load(childPath, true), child)) {
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
				const auto output = std::find_if(
					child.nodes.begin(), child.nodes.end(),
					[&](const ShaderGraphNode& value) {
						return value.id == child.outputNode;
					});
				if (output != child.nodes.end()) {
					const ShaderGraphNodeDescriptor* descriptor =
						ShaderGraphNodeRegistry::Find(output->kind);
					for (uint32_t slot = 0;
						slot < GetShaderGraphInputCount(*output); ++slot) {
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
			editSession_.MarkDirty();
		}
		return;
	}
	if (node.kind != ShaderGraphNodeKind::CustomFunction) {
		return;
	}

	{
		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphCustomFunction");
		editSession_.MarkDirty(MyGUI::InputText(
			"関数名", node.functionName).valueChanged);
		editSession_.MarkDirty(MyGUI::EnumCombo(
			"ソース", node.customFunctionSource).valueChanged);
		if (node.customFunctionSource ==
			ShaderGraphCustomFunctionSource::File) {
			AssetID functionFile = node.functionFileAsset;
			if (MyGUI::AssetReferenceField(
				"HLSLファイル", functionFile,
				context.toolContext.assetDatabase,
				{ AssetType::Shader }).valueChanged) {

				node.functionFileAsset = functionFile;
				node.functionFile.clear();
				editSession_.MarkDirty();
			}
		} else {
			TextEditSetting setting{};
			setting.multiLine = true;
			setting.size.y = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
			editSession_.MarkDirty(MyGUI::InputText(
				"関数本体", node.functionBody, setting).valueChanged);
		}
	}

	const auto removePort = [&](bool input, uint32_t index) {
		std::erase_if(editSession_.GetDraft().links,
			[&](const ShaderGraphLink& link) {
				return input ?
					(link.inputNode == node.id && link.inputSlot == index) :
					(link.outputNode == node.id && link.outputSlot == index);
			});
		for (ShaderGraphLink& link : editSession_.GetDraft().links) {
			if (input && link.inputNode == node.id && index < link.inputSlot) {
				--link.inputSlot;
			} else if (!input && link.outputNode == node.id && index < link.outputSlot) {
				--link.outputSlot;
			}
		}
		auto& ports = input ? node.inputPorts : node.outputPorts;
		ports.erase(ports.begin() + index);
		editSession_.MarkDirty();
		};
	const auto drawPorts = [&](const char* label, bool input) {
		ImGui::SeparatorText(label);
		auto& ports = input ? node.inputPorts : node.outputPorts;
		for (uint32_t index = 0; index < ports.size();) {
			ShaderGraphPort& port = ports[index];
			ImGui::PushID(static_cast<int>(index) + (input ? 0 : 1000));
			MyGUI::ScopedPropertyLabelWidth labelWidth(
				input ? "ShaderGraphCustomInput" : "ShaderGraphCustomOutput");
			editSession_.MarkDirty(MyGUI::InputText(
				"名前", port.name).valueChanged);
			const ShaderGraphValueType oldType = port.type;
			if (MyGUI::EnumCombo("型", port.type).valueChanged) {
				if (port.type == ShaderGraphValueType::Invalid ||
					port.type == ShaderGraphValueType::Texture2D ||
					port.type == ShaderGraphValueType::SamplerState) {
					port.type = oldType;
				} else {
					port.defaultValue = DefaultValueForGraphType(port.type);
					editSession_.MarkDirty();
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
		const char* addButtonLabel = input ?
			"追加##CustomFunctionInput" :
			"追加##CustomFunctionOutput";
		if (ImGui::Button(addButtonLabel, ImVec2(-FLT_MIN, 0.0f))) {
			const uint32_t suffix = static_cast<uint32_t>(ports.size() + 1);
			ports.emplace_back(ShaderGraphPort{
				.id = UUID::New(),
				.name = std::string(input ? "Input" : "Output") +
					std::to_string(suffix),
				.type = ShaderGraphValueType::Float,
				.defaultValue = DefaultValueForGraphType(
					ShaderGraphValueType::Float),
				});
			editSession_.MarkDirty();
		}
		};
	drawPorts("入力", true);
	drawPorts("出力", false);
}

void Engine::ShaderGraphEditorTool::DrawAppearancePanel() {

	if (!MyGUI::CollapsingHeader("見た目設定", false)) {
		return;
	}

	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("保存##Appearance", ImVec2(buttonWidth, 0.0f))) {

		SaveAppearanceSettings();
		editSession_.GetStatusMessage() = "見た目設定を保存しました";
	}
	ImGui::SameLine();
	if (ImGui::Button("読み込み##Appearance", ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize = appearanceSetting_.nodePreviewTextureSize;
		const bool loaded = LoadAppearanceSettings();
		if (loaded && previousTextureSize != appearanceSetting_.nodePreviewTextureSize) {

			nodePreviews_.ClearNodePreviews();
		}
		editSession_.GetStatusMessage() = loaded ? "見た目設定を読み込みました" : "保存済みの見た目設定がありません";
	}
	ImGui::SameLine();
	if (ImGui::Button(
		"元に戻す##Appearance",
		ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize =
			appearanceSetting_.nodePreviewTextureSize;
		RestoreDefaultAppearance();
		if (previousTextureSize !=
			appearanceSetting_.nodePreviewTextureSize) {

			nodePreviews_.ClearNodePreviews();
		}
		editSession_.GetStatusMessage() = "見た目設定を既定値に戻しました";
	}

	if (MyGUI::CollapsingHeader(
		"キャンバス", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphCanvasAppearance");
		MyGUI::ColorEdit(
			"背景", appearanceSetting_.canvasBackground);
		MyGUI::ColorEdit(
			"グリッド", appearanceSetting_.grid);
		MyGUI::DragFloat(
			"グリッド間隔",
			appearanceSetting_.gridSpacing,
			AppearanceFloatSetting(4.0f, 256.0f, 1.0f));
		MyGUI::DragFloat(
			"ノードスナップ間隔",
			appearanceSetting_.nodeSnapGridSize,
			AppearanceFloatSetting(0.0f, 256.0f, 1.0f));
	}
	if (MyGUI::CollapsingHeader(
		"ノード", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphNodeAppearance");
		MyGUI::ColorEdit(
			"背景", appearanceSetting_.nodeBackground);
		MyGUI::ColorEdit(
			"境界線", appearanceSetting_.nodeBorder);
		MyGUI::ColorEdit(
			"ホバー境界線",
			appearanceSetting_.hoveredNodeBorder);
		MyGUI::ColorEdit(
			"選択境界線",
			appearanceSetting_.selectedNodeBorder);
		MyGUI::ColorEdit(
			"範囲選択", appearanceSetting_.nodeSelection);
		MyGUI::ColorEdit(
			"範囲選択境界線",
			appearanceSetting_.nodeSelectionBorder);
		MyGUI::DragVector4(
			"余白", appearanceSetting_.nodePadding,
			AppearanceFloatSetting(0.0f, 64.0f));
		MyGUI::DragFloat(
			"文字スケール",
			appearanceSetting_.nodeTextScale,
			AppearanceFloatSetting(0.5f, 2.0f, 0.01f));
		MyGUI::DragFloat(
			"ノード最小幅",
			appearanceSetting_.nodeMinimumWidth,
			AppearanceFloatSetting(0.0f, 0.0f, 1.0f));
		MyGUI::DragFloat(
			"プレビュー表示サイズ",
			appearanceSetting_.nodePreviewDisplaySize,
			AppearanceFloatSetting(64.0f, 512.0f, 1.0f));
		const ValueEditResult previewTextureSizeResult =
			MyGUI::DragInt(
				"プレビュー解像度",
				appearanceSetting_.nodePreviewTextureSize,
				{
					.dragSpeed = 1.0f,
					.minValue = 32,
					.maxValue = 1024,
				});
		if (previewTextureSizeResult.editFinished) {
			ClampAppearanceSettings();
			nodePreviews_.ClearNodePreviews();
		}
		MyGUI::DragFloat(
			"角丸", appearanceSetting_.nodeRounding,
			AppearanceFloatSetting(0.0f, 32.0f));
		MyGUI::DragFloat(
			"境界線幅",
			appearanceSetting_.nodeBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat(
			"ホバー境界線幅",
			appearanceSetting_.hoveredNodeBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat(
			"選択境界線幅",
			appearanceSetting_.selectedNodeBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
	}
	if (MyGUI::CollapsingHeader(
		"接続", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphConnectionAppearance");
		MyGUI::ColorEdit(
			"リンク", appearanceSetting_.link);
		MyGUI::ColorEdit(
			"ホバーリンク",
			appearanceSetting_.hoveredLinkBorder);
		MyGUI::ColorEdit(
			"選択リンク",
			appearanceSetting_.selectedLinkBorder);
		MyGUI::ColorEdit(
			"強調リンク",
			appearanceSetting_.highlightedLinkBorder);
		MyGUI::ColorEdit(
			"リンク範囲選択",
			appearanceSetting_.linkSelection);
		MyGUI::ColorEdit(
			"リンク範囲境界線",
			appearanceSetting_.linkSelectionBorder);
		MyGUI::ColorEdit(
			"ピン範囲選択",
			appearanceSetting_.pinSelection);
		MyGUI::ColorEdit(
			"ピン範囲境界線",
			appearanceSetting_.pinSelectionBorder);
		MyGUI::DragVector2(
			"開始位置オフセット",
			appearanceSetting_.linkStartOffset,
			AppearanceFloatSetting(-128.0f, 128.0f));
		MyGUI::DragVector2(
			"終了位置オフセット",
			appearanceSetting_.linkEndOffset,
			AppearanceFloatSetting(-128.0f, 128.0f));
		MyGUI::DragFloat(
			"ピン角丸",
			appearanceSetting_.pinRounding,
			AppearanceFloatSetting(0.0f, 16.0f));
		MyGUI::DragFloat(
			"ピン境界線幅",
			appearanceSetting_.pinBorderWidth,
			AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat(
			"リンク曲率",
			appearanceSetting_.linkStrength,
			AppearanceFloatSetting(0.0f, 500.0f, 1.0f));
		MyGUI::DragFloat(
			"リンク太さ",
			appearanceSetting_.linkThickness,
			AppearanceFloatSetting(0.1f, 10.0f));
	}
	ClampAppearanceSettings();
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
		editSession_.GetStatusMessage() =
			"パラメータIDをコピーしました";
	}

	editSession_.MarkDirty(MyGUI::InputText(
		"名前", parameter.name).valueChanged);
	editSession_.MarkDirty(MyGUI::InputText(
		"参照名", parameter.referenceName).valueChanged);
	editSession_.MarkDirty(MyGUI::EnumCombo(
		"精度", parameter.precision).valueChanged);
	editSession_.MarkDirty(MyGUI::EnumCombo(
		"更新単位", parameter.scope).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox(
		"公開", parameter.exposed));

	const ShaderGraphValueType oldType = parameter.type;
	if (MyGUI::EnumCombo(
		"型", parameter.type).valueChanged) {

		if (parameter.type == ShaderGraphValueType::Invalid ||
			parameter.type == ShaderGraphValueType::SamplerState) {
			parameter.type = oldType;
		} else {
			parameter.defaultValue =
				DefaultValueForGraphType(parameter.type);
			for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {
				if (node.kind == ShaderGraphNodeKind::Parameter &&
					node.parameterID == parameter.id) {

					node.valueType = parameter.type;
				}
			}
			editSession_.MarkDirty();
		}
	}
	editSession_.MarkDirty(MyGUI::EnumCombo(
		"Semantic", parameter.semantic).valueChanged);

	switch (parameter.type) {
	case ShaderGraphValueType::Float: {
		float value =
			std::get_if<float>(&parameter.defaultValue.value) ?
			std::get<float>(parameter.defaultValue.value) : 0.0f;
		if (MyGUI::DragFloat(
			"既定値", value).valueChanged) {

			parameter.defaultValue.value = value;
			editSession_.MarkDirty();
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
			editSession_.MarkDirty();
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
			editSession_.MarkDirty();
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
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Color: {
		Color4 value =
			std::get_if<Color4>(&parameter.defaultValue.value) ?
			std::get<Color4>(parameter.defaultValue.value) :
			Color4(1.0f, 1.0f, 1.0f, 1.0f);
		if (MyGUI::ColorEdit(
			"既定値", value,
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_NoInputs).valueChanged) {

			parameter.defaultValue.value = value;
			editSession_.MarkDirty();
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
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Boolean: {
		bool value =
			std::get_if<bool>(&parameter.defaultValue.value) ?
			std::get<bool>(parameter.defaultValue.value) : false;
		if (MyGUI::Checkbox("既定値", value)) {
			parameter.defaultValue.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Integer: {
		int32_t value =
			std::get_if<int32_t>(&parameter.defaultValue.value) ?
			std::get<int32_t>(parameter.defaultValue.value) : 0;
		if (MyGUI::DragInt("既定値", value).valueChanged) {
			parameter.defaultValue.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphEditorTool::DrawPreviewSetting(const EditorToolContext& context) {

	ImGui::SeparatorText("マテリアルプレビュー");

	ECSWorld* world = context.GetWorld();
	UUID nextEntity = scenePreview_.GetTargetEntityUUID();
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphMaterialPreview");
	MyGUI::BeginPropertyRow("プレビューエンティティ");
	const float clearButtonWidth = 72.0f;
	const float fieldWidth = (std::max)(
		ImGui::GetContentRegionAvail().x -
		clearButtonWidth -
		ImGui::GetStyle().ItemSpacing.x,
		1.0f);
	const ValueEditResult result =
		MyGUI::EntityReferenceField(
			"", nextEntity, world,
			{
				.useAutoPropertyRow = false,
				.buttonSize = ImVec2(
					fieldWidth,
					ImGui::GetFrameHeight()),
			});
	ImGui::SameLine();
	const bool hasPreviewEntity =
		scenePreview_.GetTargetEntityUUID() != UUID{};
	ImGui::BeginDisabled(!hasPreviewEntity);
	const bool clear =
		ImGui::Button(
			"解除",
			ImVec2(
				clearButtonWidth,
				ImGui::GetFrameHeight()));
	ImGui::EndDisabled();
	MyGUI::EndPropertyRow();

	if (clear) {
		RestorePreviewMaterial(context);
		scenePreview_.GetTargetEntityUUID() = {};
		return;
	}
	if (!result.valueChanged ||
		nextEntity == scenePreview_.GetTargetEntityUUID()) {

		return;
	}

	RestorePreviewMaterial(context);
	scenePreview_.GetTargetEntityUUID() = nextEntity;
	if (!scenePreview_.GetTargetEntityUUID()) {
		return;
	}
	if ((!editSession_.GetPreviewMaterialID() || editSession_.NeedsCompile()) &&
		!SaveAndCompile(context)) {

		return;
	}
	ApplyPreviewMaterial(context);
}

void Engine::ShaderGraphEditorTool::DrawGraph(const EditorToolContext& context) {

	if (!editSession_.IsLoaded()) {
		return;
	}
	if (!nodeEditor_) {
		ed::Config config{};
		config.SettingsFile = nullptr;
		nodeEditor_ = ed::CreateEditor(&config);
		restoreNodePositions_ = true;
	}

	nodePreviews_.UpdateNodePreviews(context, editSession_.GetDraft(), appearanceSetting_, editSession_.GetStatusMessage());
	ed::SetCurrentEditor(nodeEditor_);
	ApplyAppearanceSettings();
	ed::Begin("ShaderGraphNodeEditor");
	pinAddresses_.clear();
	for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		DrawNode(node);
	}
	for (ShaderGraphGroup& group : editSession_.GetDraft().groups) {
		DrawGroup(group);
	}
	for (const ShaderGraphLink& link : editSession_.GetDraft().links) {
		ed::Link(
			ed::LinkId(ToNodeEditorID(link.id)),
			ed::PinId(MakePinID(
				link.outputNode, false,
				link.outputSlot)),
			ed::PinId(MakePinID(
				link.inputNode, true,
				link.inputSlot)),
			ToImVec4(appearanceSetting_.link),
			appearanceSetting_.linkThickness);
	}

	if (restoreNodePositions_) {
		for (const ShaderGraphNode& node : editSession_.GetDraft().nodes) {
			ed::SetNodePosition(
				ed::NodeId(ToNodeEditorID(node.id)),
				ImVec2(node.position.x, node.position.y));
		}
		for (const ShaderGraphGroup& group : editSession_.GetDraft().groups) {
			const ed::NodeId groupID(
				ToNodeEditorID(group.id));
			ed::SetNodePosition(
				groupID,
				ImVec2(
					group.position.x,
					group.position.y));
			ed::SetGroupSize(
				groupID,
				ImVec2(group.size.x, group.size.y));
		}
		restoreNodePositions_ = false;
	}

	const ImVec4 linkColor =
		ToImVec4(appearanceSetting_.link);
	if (ed::BeginCreate(
		linkColor,
		appearanceSetting_.linkThickness)) {

		ed::PinId firstPin{};
		ed::PinId secondPin{};
		if (ed::QueryNewLink(
			&firstPin, &secondPin,
			linkColor,
			appearanceSetting_.linkThickness) &&
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
				if (ed::AcceptNewItem(
					linkColor,
					appearanceSetting_.linkThickness)) {

					std::erase_if(
						editSession_.GetDraft().links,
						[&](const ShaderGraphLink& link) {
							return link.inputNode == input.node &&
								link.inputSlot == input.slot;
						});
					editSession_.GetDraft().links.emplace_back(
						ShaderGraphLink{
							.id = UUID::New(),
							.outputNode = output.node,
							.outputSlot = output.slot,
							.inputNode = input.node,
							.inputSlot = input.slot,
						});
					editSession_.MarkDirty();
				}
			} else {
				ed::RejectNewItem(
					ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
					appearanceSetting_.linkThickness);
			}
		}
	}
	ed::EndCreate();

	if (commandPanelFocused_) {
		if (ed::BeginDelete()) {
			ed::LinkId linkID{};
			while (ed::QueryDeletedLink(&linkID)) {
				if (ed::AcceptDeletedItem()) {
					const uint64_t id =
						static_cast<uint64_t>(linkID.Get());
					std::erase_if(
						editSession_.GetDraft().links,
						[&](const ShaderGraphLink& link) {
							return link.id.value == id;
						});
					editSession_.MarkDirty();
				}
			}

			ed::NodeId nodeID{};
			while (ed::QueryDeletedNode(&nodeID)) {
				const UUID id{
					static_cast<uint64_t>(nodeID.Get())
				};
				if (id == editSession_.GetDraft().outputNode) {
					ed::RejectDeletedItem();
				} else if (ed::AcceptDeletedItem()) {
					const auto group = std::find_if(
						editSession_.GetDraft().groups.begin(),
						editSession_.GetDraft().groups.end(),
						[&](const ShaderGraphGroup& value) {
							return value.id == id;
						});
					if (group != editSession_.GetDraft().groups.end()) {
						RemoveGroup(id);
					} else {
						RemoveNode(id);
					}
				}
			}
		}
		ed::EndDelete();
	}

	const ImVec2 canvasMousePosition =
		ImGui::GetMousePos();
	ed::Suspend();
	DrawNodeValuePopup();
	ed::NodeId contextNodeID{};
	if (ed::ShowNodeContextMenu(&contextNodeID)) {
		contextNode_ = UUID{
			static_cast<uint64_t>(
				contextNodeID.Get())
		};
		ImGui::OpenPopup(kNodeContextPopup);
	} else if (ed::ShowBackgroundContextMenu()) {
		createNodePosition_ =
			Vector2(
				canvasMousePosition.x,
				canvasMousePosition.y);
		ImGui::OpenPopup(kCreateNodePopup);
	}
	DrawContextMenus();
	ed::Resume();
	ed::End();
	const ImGuiIO& io = ImGui::GetIO();
	if (commandPanelFocused_ &&
		!io.WantTextInput &&
		!ImGui::IsAnyItemActive() &&
		io.KeyCtrl) {
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			CopySelection();
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
			PasteSelection();
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
			CopySelection();
			PasteSelection();
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			SaveAndCompile(context);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (io.KeyShift) {
				RedoGraph();
			} else {
				UndoGraph();
			}
		}
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
			RedoGraph();
		}
	}
	ed::SetCurrentEditor(nullptr);
}

void Engine::ShaderGraphEditorTool::DrawGroup(ShaderGraphGroup& group) {

	ImGui::PushFont(
		nullptr,
		ImGui::GetStyle().FontSizeBase *
		appearanceSetting_.nodeTextScale);
	ed::PushStyleVar(
		ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	const ed::NodeId groupID(ToNodeEditorID(group.id));
	const ImVec2 editorGroupSize =
		ed::GetNodeSize(groupID);
	const float groupWidth =
		0.0f < editorGroupSize.x ?
		editorGroupSize.x : group.size.x;
	ed::BeginNode(
		groupID);
	ImGui::PushID(
		static_cast<int>(group.id.value));

	const ImVec2 headerMinimum =
		ImGui::GetCursorScreenPos();
	if (editingGroup_ == group.id) {
		const float editWidth =
			(std::min)(groupWidth, kGroupNameEditWidth);
		ImGui::SetCursorScreenPos(
			ImVec2(
				headerMinimum.x +
				(groupWidth - editWidth) * 0.5f,
				headerMinimum.y));
		ImGui::SetNextItemWidth(editWidth);
		if (requestGroupNameFocus_) {
			ImGui::SetKeyboardFocusHere();
			requestGroupNameFocus_ = false;
		}

		const std::string previousName = group.name;
		const bool committed = ImGui::InputText(
			"##GroupName",
			&group.name,
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_AutoSelectAll);
		const bool deactivated =
			ImGui::IsItemDeactivated();
		if (group.name != previousName) {
			editSession_.MarkDirty();
		}
		if (committed || deactivated) {
			if (group.name.empty()) {
				group.name = "Group";
				editSession_.MarkDirty();
			}
			editingGroup_ = UUID{};
		}
	} else {
		const char* name =
			group.name.empty() ? "Group" :
			group.name.c_str();
		const float textWidth =
			ImGui::CalcTextSize(name).x;
		ImGui::SetCursorScreenPos(
			ImVec2(
				headerMinimum.x +
				(groupWidth - textWidth) * 0.5f,
				headerMinimum.y));
		ImGui::TextUnformatted(name);
		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(
				ImGuiMouseButton_Left)) {

			editingGroup_ = group.id;
			requestGroupNameFocus_ = true;
		}
	}

	const float groupMinimumY =
		ImGui::GetCursorScreenPos().y;
	ImGui::SetCursorScreenPos(
		ImVec2(headerMinimum.x, groupMinimumY));
	ed::Group(
		ImVec2(groupWidth, group.size.y));
	ImGui::PopID();
	ed::EndNode();
	std::vector<ed::NodeId> memberIDs{};
	for (const ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		if (node.groupID == group.id) {
			memberIDs.emplace_back(
				ToNodeEditorID(node.id));
		}
	}
	ed::SetGroupMembers(
		groupID,
		memberIDs.empty() ? nullptr : memberIDs.data(),
		static_cast<int>(memberIDs.size()));
	ed::PopStyleVar();

	if (ed::BeginGroupHint(
		groupID)) {

		const ImVec2 groupMinimum =
			ed::GetGroupMin();
		const ImVec2 groupMaximum =
			ed::GetGroupMax();
		const Vector2 groupSize{
			groupMaximum.x - groupMinimum.x,
			groupMaximum.y - groupMinimum.y,
		};
		if (group.size.x != groupSize.x ||
			group.size.y != groupSize.y) {

			group.size = groupSize;
			editSession_.MarkDirty();
		}
	}
	ed::EndGroupHint();
	ImGui::PopFont();
}

void Engine::ShaderGraphEditorTool::DrawNode(ShaderGraphNode& node) {

	ImGui::PushFont(
		nullptr,
		ImGui::GetStyle().FontSizeBase *
		appearanceSetting_.nodeTextScale);
	ed::BeginNode(
		ed::NodeId(ToNodeEditorID(node.id)));
	ImGui::PushID(
		static_cast<int>(node.id.value));
	const float nodeWidth =
		CalculateNodeWidth(node);
	const ImVec2 headerMinimum =
		ImGui::GetCursorScreenPos();
	ImGui::TextUnformatted(
		GetShaderGraphNodeName(node.kind).data());
	float headerHeight =
		ImGui::GetItemRectSize().y;
	if (IsPreviewableNode(node.kind)) {

		const float buttonSize =
			ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos(
			ImVec2(
				headerMinimum.x +
				nodeWidth - buttonSize,
				headerMinimum.y));
		if (ImGui::ArrowButton(
			"##NodePreview",
			node.previewExpanded ?
			ImGuiDir_Down :
			ImGuiDir_Right)) {

			node.previewExpanded =
				!node.previewExpanded;
			editSession_.MarkDirty();
		}
		headerHeight =
			(std::max)(
				headerHeight,
				ImGui::GetItemRectSize().y);
	}
	ImGui::SetCursorScreenPos(
		ImVec2(
			headerMinimum.x,
			headerMinimum.y + headerHeight));
	DrawNodeSeparator(nodeWidth);

	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(
			editSession_.GetDraft().parameters.begin(),
			editSession_.GetDraft().parameters.end(),
			[&](const ShaderGraphParameter& value) {
				return value.id == node.parameterID;
			});
		if (parameter != editSession_.GetDraft().parameters.end()) {
			ImGui::TextDisabled(
				"%s", parameter->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::Keyword) {
		const auto keyword = std::find_if(
			editSession_.GetDraft().keywords.begin(), editSession_.GetDraft().keywords.end(),
			[&](const ShaderGraphKeyword& value) {
				return value.id == node.keywordID;
			});
		if (keyword != editSession_.GetDraft().keywords.end()) {
			ImGui::TextDisabled("%s", keyword->name.c_str());
		}
	}
	if (node.kind == ShaderGraphNodeKind::CustomFunction &&
		!node.functionName.empty()) {
		ImGui::TextDisabled("%s", node.functionName.c_str());
	}
	if (node.kind == ShaderGraphNodeKind::Constant ||
		node.kind == ShaderGraphNodeKind::TextureSample) {

		DrawNodeValue(node, nodeWidth);
	}
	DrawNodePins(node, nodeWidth);
	nodePreviews_.DrawNodePreview(node, nodeWidth, appearanceSetting_);
	ImGui::PopID();
	ed::EndNode();
	ImGui::PopFont();
}

void Engine::ShaderGraphEditorTool::DrawNodePins(const ShaderGraphNode& node, float nodeWidth) {

	const uint32_t inputCount =
		GetShaderGraphInputCount(node);
	const uint32_t outputCount =
		GetShaderGraphOutputCount(node);
	const uint32_t rowCount =
		(std::max)(inputCount, outputCount);
	if (rowCount == 0) {
		ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
		return;
	}

	const ImVec2 rowsMinimum =
		ImGui::GetCursorScreenPos();
	const float rowHeight =
		ImGui::GetTextLineHeightWithSpacing();
	for (uint32_t row = 0;
		row < rowCount; ++row) {

		const float rowY =
			rowsMinimum.y + rowHeight * row;
		if (row < inputCount) {
			const std::string name =
				"> " + std::string(
					GetShaderGraphInputName(
						node, row));
			ImGui::SetCursorScreenPos(
				ImVec2(rowsMinimum.x, rowY));

			const uintptr_t pinID =
				MakePinID(node.id, true, row);
			pinAddresses_[pinID] =
				PinAddress{ node.id, row, true };
			ed::BeginPin(
				ed::PinId(pinID),
				ed::PinKind::Input);
			ImGui::TextUnformatted(name.c_str());
			const ImVec2 pinMinimum =
				ImGui::GetItemRectMin();
			const ImVec2 pinMaximum =
				ImGui::GetItemRectMax();
			const ImVec2 pinCenter{
				pinMinimum.x +
					appearanceSetting_.linkEndOffset.x,
				(pinMinimum.y + pinMaximum.y) * 0.5f +
					appearanceSetting_.linkEndOffset.y,
			};
			ed::PinPivotRect(pinCenter, pinCenter);
			ed::EndPin();
		}
		if (row < outputCount) {
			const std::string name =
				std::string(
					GetShaderGraphOutputName(
						node, row)) +
				" >";
			const float textWidth =
				ImGui::CalcTextSize(name.c_str()).x;
			ImGui::SetCursorScreenPos(
				ImVec2(
					rowsMinimum.x +
					nodeWidth - textWidth,
					rowY));

			const uintptr_t pinID =
				MakePinID(node.id, false, row);
			pinAddresses_[pinID] =
				PinAddress{ node.id, row, false };
			ed::BeginPin(
				ed::PinId(pinID),
				ed::PinKind::Output);
			ImGui::TextUnformatted(name.c_str());
			const ImVec2 pinMinimum =
				ImGui::GetItemRectMin();
			const ImVec2 pinMaximum =
				ImGui::GetItemRectMax();
			const ImVec2 pinCenter{
				pinMaximum.x +
					appearanceSetting_.linkStartOffset.x,
				(pinMinimum.y + pinMaximum.y) * 0.5f +
					appearanceSetting_.linkStartOffset.y,
			};
			ed::PinPivotRect(pinCenter, pinCenter);
			ed::EndPin();
		}
	}

	ImGui::SetCursorScreenPos(
		ImVec2(
			rowsMinimum.x,
			rowsMinimum.y + rowHeight * rowCount));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphEditorTool::DrawNodeSeparator(float nodeWidth) const {

	const ImVec2 minimum =
		ImGui::GetCursorScreenPos();
	const float height =
		ImGui::GetStyle().ItemSpacing.y;
	const float lineY =
		minimum.y + height * 0.5f;
	ImGui::GetWindowDrawList()->AddLine(
		ImVec2(minimum.x, lineY),
		ImVec2(minimum.x + nodeWidth, lineY),
		ImGui::GetColorU32(ImGuiCol_Separator));
	ImGui::Dummy(ImVec2(nodeWidth, height));
}

float Engine::ShaderGraphEditorTool::CalculateNodeWidth(const ShaderGraphNode& node) const {

	float nodeWidth =
		(std::max)(
			appearanceSetting_.nodeMinimumWidth,
			ImGui::CalcTextSize(
				GetShaderGraphNodeName(
					node.kind).data()).x +
			(IsPreviewableNode(node.kind) ?
				ImGui::GetStyle().ItemSpacing.x +
				ImGui::GetFrameHeight() : 0.0f));
	if (node.kind == ShaderGraphNodeKind::Parameter) {
		const auto parameter = std::find_if(
			editSession_.GetDraft().parameters.begin(),
			editSession_.GetDraft().parameters.end(),
			[&](const ShaderGraphParameter& value) {
				return value.id == node.parameterID;
			});
		if (parameter != editSession_.GetDraft().parameters.end()) {
			nodeWidth =
				(std::max)(
					nodeWidth,
					ImGui::CalcTextSize(
						parameter->name.c_str()).x);
		}
	}
	if (IsPreviewableNode(node.kind) &&
		node.previewExpanded) {
		nodeWidth =
			(std::max)(
				nodeWidth,
				appearanceSetting_.
				nodePreviewDisplaySize);
	}

	float inputWidth = 0.0f;
	for (uint32_t slot = 0;
		slot < GetShaderGraphInputCount(node);
		++slot) {

		const std::string name =
			"> " + std::string(
				GetShaderGraphInputName(
					node, slot));
		inputWidth =
			(std::max)(
				inputWidth,
				ImGui::CalcTextSize(name.c_str()).x);
	}
	float outputWidth = 0.0f;
	for (uint32_t slot = 0;
		slot < GetShaderGraphOutputCount(node);
		++slot) {

		const std::string name =
			std::string(
				GetShaderGraphOutputName(
					node, slot)) +
			" >";
		outputWidth =
			(std::max)(
				outputWidth,
				ImGui::CalcTextSize(name.c_str()).x);
	}
	if (inputWidth != 0.0f &&
		outputWidth != 0.0f) {

		nodeWidth =
			(std::max)(
				nodeWidth,
				inputWidth +
				kNodePinColumnGap +
				outputWidth);
	} else {
		nodeWidth =
			(std::max)(
				nodeWidth,
				(std::max)(
					inputWidth,
					outputWidth));
	}
	return nodeWidth;
}

void Engine::ShaderGraphEditorTool::DrawNodeValue(ShaderGraphNode& node, float nodeWidth) {

	if (node.kind == ShaderGraphNodeKind::Constant) {
		DrawNodeValueTypeButton(node, nodeWidth);
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
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragFloat(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float2: {
		Vector2 value =
			std::get_if<Vector2>(&node.value.value) ?
			std::get<Vector2>(node.value.value) :
			Vector2{};
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector2(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float3: {
		Vector3 value =
			std::get_if<Vector3>(&node.value.value) ?
			std::get<Vector3>(node.value.value) :
			Vector3{};
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector3(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Float4: {
		Vector4 value =
			std::get_if<Vector4>(&node.value.value) ?
			std::get<Vector4>(node.value.value) :
			Vector4{};
		FloatEditSetting setting{};
		setting.propertyRow =
			NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragVector4(
			"値", value, setting).valueChanged) {

			node.value.value = value;
			editSession_.MarkDirty();
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
		DrawNodeColorButton(
			node, label, value, nodeWidth);
		break;
	}
	case ShaderGraphValueType::Boolean: {
		bool value =
			std::get_if<bool>(&node.value.value) ?
			std::get<bool>(node.value.value) : false;
		if (MyGUI::Checkbox(
			"値", value,
			NodeValueRowSetting("値", nodeWidth))) {
			node.value.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	case ShaderGraphValueType::Integer: {
		int32_t value =
			std::get_if<int32_t>(&node.value.value) ?
			std::get<int32_t>(node.value.value) : 0;
		IntEditSetting setting{};
		setting.propertyRow = NodeValueRowSetting("値", nodeWidth);
		if (MyGUI::DragInt("値", value, setting).valueChanged) {
			node.value.value = value;
			editSession_.MarkDirty();
		}
		break;
	}
	default:
		break;
	}
}

void Engine::ShaderGraphEditorTool::RequestNodeValuePopup(
	UUID nodeID, NodeValuePopupKind kind,
	const Vector2& anchor, float width,
	uint32_t viewportID) {

	nodeValuePopupNode_ = nodeID;
	nodeValuePopupKind_ = kind;
	nodeValuePopupAnchor_ = anchor;
	nodeValuePopupWidth_ = width;
	nodeValuePopupViewportID_ = viewportID;
	requestNodeValuePopup_ = true;
}

void Engine::ShaderGraphEditorTool::DrawNodeValueTypeButton(ShaderGraphNode& node, float nodeWidth) {

	if (!MyGUI::BeginPropertyRow(
		"型", NodeValueRowSetting(
			"型", nodeWidth))) {

		return;
	}
	const float width =
		(std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
	ImGui::SetNextItemWidth(width);
	ImGui::PushStyleColor(
		ImGuiCol_Button,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(
		ImGuiCol_ButtonHovered,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
	ImGui::PushStyleColor(
		ImGuiCol_ButtonActive,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
	ImGui::PushStyleVar(
		ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
	const bool pressed = ImGui::Button(
		EnumAdapter<ShaderGraphValueType>::ToString(node.valueType),
		ImVec2(width, ImGui::GetFrameHeight()));
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(3);

	const ImVec2 itemMin = ImGui::GetItemRectMin();
	const ImVec2 itemMax = ImGui::GetItemRectMax();
	const ImVec2 screenMin = ed::CanvasToScreen(itemMin);
	const ImVec2 screenMax = ed::CanvasToScreen(itemMax);
	const ImVec2 arrowPosition(
		itemMax.x - ImGui::GetFrameHeight() +
		ImGui::GetStyle().FramePadding.x,
		itemMin.y + ImGui::GetStyle().FramePadding.y);
	ImGui::RenderArrow(
		ImGui::GetWindowDrawList(), arrowPosition,
		ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down);
	if (pressed) {

		RequestNodeValuePopup(
			node.id, NodeValuePopupKind::ValueType,
			Vector2(screenMin.x, screenMax.y),
			screenMax.x - screenMin.x,
			ImGui::GetWindowViewport()->ID);
	}
	MyGUI::EndPropertyRow();
}

void Engine::ShaderGraphEditorTool::DrawNodeColorButton(
	const ShaderGraphNode& node, const char* label,
	const Color4& value, float nodeWidth) {

	if (!MyGUI::BeginPropertyRow(
		label, NodeValueRowSetting(
			label, nodeWidth))) {

		return;
	}
	const ImVec4 color(value.r, value.g, value.b, value.a);
	const bool pressed = ImGui::ColorButton(
		"##Value", color,
		ImGuiColorEditFlags_Float |
		ImGuiColorEditFlags_NoInputs);
	const ImVec2 itemMin = ImGui::GetItemRectMin();
	const ImVec2 itemMax = ImGui::GetItemRectMax();
	const ImVec2 screenMin = ed::CanvasToScreen(itemMin);
	const ImVec2 screenMax = ed::CanvasToScreen(itemMax);
	if (pressed) {

		RequestNodeValuePopup(
			node.id, NodeValuePopupKind::Color,
			Vector2(
				screenMin.x - 1.0f,
				screenMax.y + ImGui::GetStyle().ItemSpacing.y),
			0.0f,
			ImGui::GetWindowViewport()->ID);
	}
	MyGUI::EndPropertyRow();
}

void Engine::ShaderGraphEditorTool::DrawNodeValuePopup() {

	if (requestNodeValuePopup_) {
		ImGui::OpenPopup(kNodeValuePopup);
		requestNodeValuePopup_ = false;
	}
	if (ImGui::IsPopupOpen(kNodeValuePopup)) {
		ImGui::SetNextWindowViewport(
			nodeValuePopupViewportID_);
		ImGui::SetNextWindowPos(
			ImVec2(
				nodeValuePopupAnchor_.x,
				nodeValuePopupAnchor_.y),
			ImGuiCond_Appearing);
		if (nodeValuePopupKind_ ==
			NodeValuePopupKind::ValueType) {

			ImGui::SetNextWindowSizeConstraints(
				ImVec2(nodeValuePopupWidth_, 0.0f),
				ImVec2(
					FLT_MAX,
					ImGui::GetTextLineHeightWithSpacing() * 8.0f +
					ImGui::GetStyle().WindowPadding.y * 2.0f));
		}
	}

	if (!ImGui::BeginPopup(kNodeValuePopup)) {
		if (!ImGui::IsPopupOpen(kNodeValuePopup)) {
			nodeValuePopupNode_ = UUID{};
			nodeValuePopupKind_ =
				NodeValuePopupKind::None;
		}
		return;
	}

	const auto found = std::find_if(
		editSession_.GetDraft().nodes.begin(), editSession_.GetDraft().nodes.end(),
		[&](const ShaderGraphNode& node) {
			return node.id == nodeValuePopupNode_;
		});
	if (found == editSession_.GetDraft().nodes.end()) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	ShaderGraphNode& node = *found;
	switch (nodeValuePopupKind_) {
	case NodeValuePopupKind::ValueType:
		for (uint32_t index = 0;
			index < EnumAdapter<ShaderGraphValueType>::GetEnumCount();
			++index) {

			const ShaderGraphValueType valueType =
				EnumAdapter<ShaderGraphValueType>::GetValue(index);
			if (valueType == ShaderGraphValueType::Invalid ||
				valueType == ShaderGraphValueType::Texture2D ||
				valueType == ShaderGraphValueType::SamplerState) {

				continue;
			}
			if (ImGui::Selectable(
				EnumAdapter<ShaderGraphValueType>::ToString(valueType),
				node.valueType == valueType)) {

				node.valueType = valueType;
				node.value = DefaultValueForGraphType(valueType);
				editSession_.MarkDirty();
				ImGui::CloseCurrentPopup();
			}
		}
		break;
	case NodeValuePopupKind::Color:
	{
		Color4 value =
			std::get_if<Color4>(&node.value.value) ?
			std::get<Color4>(node.value.value) :
			Color4::White();
		float color[4] = {
			value.r, value.g, value.b, value.a
		};
		ImGui::SetNextItemWidth(
			ImGui::GetFrameHeight() * 12.0f);
		if (ImGui::ColorPicker4(
			"##NodeColorPicker", color,
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_NoLabel |
			ImGuiColorEditFlags_AlphaPreviewHalf)) {

			node.value.value = Color4(
				color[0], color[1], color[2], color[3]);
			editSession_.MarkDirty();
		}
		break;
	}
	case NodeValuePopupKind::None:
	default:
		ImGui::CloseCurrentPopup();
		break;
	}
	ImGui::EndPopup();
}

void Engine::ShaderGraphEditorTool::DrawContextMenus() {

	if (ImGui::BeginPopup(kNodeContextPopup)) {
		const auto group = std::find_if(
			editSession_.GetDraft().groups.begin(),
			editSession_.GetDraft().groups.end(),
			[&](const ShaderGraphGroup& value) {
				return value.id == contextNode_;
			});
		const auto node = std::find_if(
			editSession_.GetDraft().nodes.begin(),
			editSession_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) {
				return value.id == contextNode_;
			});

		if (group != editSession_.GetDraft().groups.end()) {
			ImGui::TextUnformatted(group->name.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("グループを複製")) {
				DuplicateGroup(contextNode_);
			}
			if (ImGui::MenuItem("グループを削除")) {
				RemoveGroup(contextNode_);
			}
		} else {
			if (node != editSession_.GetDraft().nodes.end()) {
				ImGui::TextUnformatted(
					GetShaderGraphNodeName(
						node->kind).data());
			}
			ImGui::Separator();
			if (node != editSession_.GetDraft().nodes.end() &&
				IsPreviewableNode(node->kind)) {

				if (ImGui::MenuItem(
					"プレビューを表示",
					nullptr,
					node->previewExpanded)) {

					node->previewExpanded =
						!node->previewExpanded;
					editSession_.MarkDirty();
				}
				ImGui::Separator();
			}
			ImGui::BeginDisabled(
				node == editSession_.GetDraft().nodes.end() ||
				contextNode_ == editSession_.GetDraft().outputNode);
			if (ImGui::MenuItem("ノードを複製")) {
				DuplicateNode(contextNode_);
			}
			if (ImGui::MenuItem("ノードを削除")) {
				RemoveNode(contextNode_);
			}
			ImGui::EndDisabled();

			const bool canGroup =
				2 <= GetSelectedGraphNodes().size();
			ImGui::BeginDisabled(!canGroup);
			if (ImGui::MenuItem(
				"選択ノードをグループ化")) {

				GroupSelectedNodes();
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}

	DrawNodeCreationMenu();
}

void Engine::ShaderGraphEditorTool::DrawNodeCreationMenu() {

	if (!ImGui::BeginPopup(kCreateNodePopup)) {
		return;
	}

	const bool canGroup =
		2 <= GetSelectedGraphNodes().size();
	ImGui::BeginDisabled(!canGroup);
	if (ImGui::MenuItem(
		"選択ノードをグループ化")) {

		GroupSelectedNodes();
	}
	ImGui::EndDisabled();
	if (ImGui::BeginMenu("プレビュー")) {
		if (ImGui::MenuItem("すべて展開")) {
			for (ShaderGraphNode& node :
				editSession_.GetDraft().nodes) {

				if (IsPreviewableNode(node.kind)) {
					node.previewExpanded = true;
				}
			}
			editSession_.MarkDirty();
		}
		if (ImGui::MenuItem("すべて折りたたむ")) {
			for (ShaderGraphNode& node :
				editSession_.GetDraft().nodes) {

				if (IsPreviewableNode(node.kind)) {
					node.previewExpanded = false;
				}
			}
			editSession_.MarkDirty();
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	ImGui::SetNextItemWidth(280.0f);
	ImGui::InputTextWithHint(
		"##NodeSearch", "ノードを検索",
		&nodeSearch_);
	if (editSession_.GetDraft().domain == ShaderGraphDomain::Surface &&
		SupportsShaderGraphVertexOutput(editSession_.GetDraft().target) &&
		!editSession_.GetDraft().vertexOutputNode &&
		ImGui::MenuItem("頂点出力を追加")) {

		AddNode(ShaderGraphNodeKind::VertexOutput,
			createNodePosition_);
	}
	if (ImGui::BeginMenu("定数")) {
		const std::array constantTypes{
			ShaderGraphValueType::Float,
			ShaderGraphValueType::Float2,
			ShaderGraphValueType::Float3,
			ShaderGraphValueType::Float4,
			ShaderGraphValueType::Color,
			ShaderGraphValueType::Boolean,
			ShaderGraphValueType::Integer,
		};
		for (ShaderGraphValueType type : constantTypes) {
			if (ImGui::MenuItem(
				EnumAdapter<ShaderGraphValueType>::ToString(type))) {
				AddConstantNode(type, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}

	const std::string search = Algorithm::ToLower(nodeSearch_);
	const auto isCreatable = [this](
		const ShaderGraphNodeDescriptor& descriptor) {

			if (descriptor.kind == ShaderGraphNodeKind::RayTrace) {
				return editSession_.GetDraft().domain == ShaderGraphDomain::RayTracingEffect;
			}
			return descriptor.kind != ShaderGraphNodeKind::SurfaceOutput &&
				descriptor.kind != ShaderGraphNodeKind::UnlitOutput &&
				descriptor.kind != ShaderGraphNodeKind::PostProcessOutput &&
				descriptor.kind != ShaderGraphNodeKind::RayTracingOutput &&
				descriptor.kind != ShaderGraphNodeKind::VertexOutput &&
				descriptor.kind != ShaderGraphNodeKind::Parameter &&
				descriptor.kind != ShaderGraphNodeKind::Constant &&
				descriptor.kind != ShaderGraphNodeKind::Keyword;
		};
	const auto& descriptors =
		ShaderGraphNodeRegistry::GetDescriptors();
	if (!search.empty()) {
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (!isCreatable(descriptor)) {
				continue;
			}
			const std::string searchable = Algorithm::ToLower(
				std::string(descriptor.name) + " " +
				std::string(descriptor.category));
			if (searchable.find(search) != std::string::npos &&
				ImGui::MenuItem(descriptor.name.data())) {
				AddNode(descriptor.kind, createNodePosition_);
			}
		}
	} else {
		std::vector<std::string_view> categories;
		for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
			if (isCreatable(descriptor) &&
				std::find(categories.begin(), categories.end(), descriptor.category) == categories.end()) {
				categories.emplace_back(descriptor.category);
			}
		}
		for (std::string_view category : categories) {
			if (!ImGui::BeginMenu(category.data())) {
				continue;
			}
			for (const ShaderGraphNodeDescriptor& descriptor : descriptors) {
				if (isCreatable(descriptor) && descriptor.category == category &&
					ImGui::MenuItem(descriptor.name.data())) {
					AddNode(descriptor.kind, createNodePosition_);
				}
			}
			ImGui::EndMenu();
		}
	}
	if (ImGui::BeginMenu("パラメータ")) {
		for (const ShaderGraphParameter& parameter :
			editSession_.GetDraft().parameters) {

			if (ImGui::MenuItem(
				parameter.name.c_str())) {

				AddParameterNode(
					parameter.id,
					createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("キーワード")) {
		for (const ShaderGraphKeyword& keyword : editSession_.GetDraft().keywords) {
			if (ImGui::MenuItem(keyword.name.c_str())) {
				AddKeywordNode(keyword.id, createNodePosition_);
			}
		}
		ImGui::EndMenu();
	}
	ImGui::EndPopup();
}

void Engine::ShaderGraphEditorTool::ImportGraphSettings(const EditorToolContext& context, AssetID source) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	const AssetMeta* meta = database ? database->Find(source) : nullptr;
	if (!editSession_.IsLoaded() || !meta || source == editSession_.GetAssetID()) {
		editSession_.GetStatusMessage() = "別のMaterialまたはShaderGraphを指定してください";
		return;
	}
	ShaderGraphAsset imported;
	const auto resolver = [database](AssetID id, AssetType type, nlohmann::json& data) {
		const AssetMeta* dependency = database->Find(id);
		if (!dependency || (type != AssetType::Unknown && dependency->type != type)) {
			return false;
		}
		const auto path = database->ResolveFullPath(id);
		if (path.empty() || !std::filesystem::exists(path)) {
			return false;
		}
		if (type == AssetType::Texture || type == AssetType::Unknown) {
			data = { { "path", Algorithm::PathToUTF8(path) } };
			return true;
		}
		data = JsonAdapter::Load(path, false);
		return !data.is_null();
	};
	if (!ShaderGraphSettingsImporter::Import(editSession_.GetDraft(), source, meta->type,
		resolver, imported, editSession_.GetStatusMessage())) {
		return;
	}
	CaptureNodePositions();
	editSession_.CaptureHistory();
	RestorePreviewMaterial(context);
	// プレビューの自動保存で取り込み直後の内容を確定しない
	scenePreview_.GetTargetEntityUUID() = {};
	scenePreview_.GetCompileDeadline() = 0.0;
	nodePreviews_.ClearNodePreviews();
	editSession_.Import(std::move(imported));
	selectedParameter_ = -1;
	editingGroup_ = {};
	contextNode_ = {};
	ResetNodeEditor();
	restoreNodePositions_ = true;
	editSession_.GetStatusMessage() = "設定をインポートしました。保存してください";
}

bool Engine::ShaderGraphEditorTool::CreateGraph(const EditorToolContext& context) {

	RestorePreviewMaterial(context);
	AssetDatabase* database =
		context.toolContext.assetDatabase;
	if (!database || createAssetPath_.empty()) {
		editSession_.GetStatusMessage() = "作成先を設定してください";
		return false;
	}

	const std::filesystem::path path =
		database->ResolveAssetPath(createAssetPath_);
	if (path.empty()) {
		editSession_.GetStatusMessage() = "GameAssets内を指定してください";
		return false;
	}
	if (std::filesystem::exists(path)) {
		editSession_.GetStatusMessage() = "同名のグラフが存在します";
		return false;
	}

	const std::string name = GraphFileStem(path);
	ShaderGraphAsset graph{};
	if (createDomain_ == ShaderGraphDomain::PostProcess) {
		graph = CreateDefaultPostProcessShaderGraph(name);
	} else if (createDomain_ == ShaderGraphDomain::RayTracingEffect) {
		graph = CreateDefaultRayTracingEffectShaderGraph(name);
	} else {
		graph = CreateDefaultSurfaceShaderGraph(name, createTarget_);
	}
	JsonAdapter::Save(path, ToJson(graph));

	const std::string assetPath =
		RuntimePaths::ToAssetPath(path);
	const AssetID assetID =
		database->ImportOrGet(
			assetPath, AssetType::ShaderGraph);
	if (!assetID) {
		editSession_.GetStatusMessage() =
			"グラフをAssetDatabaseへ登録できませんでした";
		return false;
	}
	editSession_.GetStatusMessage() = "グラフを作成しました";
	return LoadGraph(context, assetID);
}

bool Engine::ShaderGraphEditorTool::ApplyPreviewMaterial(const EditorToolContext& context) {

	return scenePreview_.ApplyPreviewMaterial(context, editSession_.GetDraft().target, editSession_.GetPreviewMaterialID(), editSession_.GetStatusMessage());
}

void Engine::ShaderGraphEditorTool::RestorePreviewMaterial(const EditorToolContext& context) {

	scenePreview_.RestorePreviewMaterial(context);
}

void Engine::ShaderGraphEditorTool::UpdateMaterialPreview(const EditorToolContext& context) {

	if (editSession_.GetDraft().domain != ShaderGraphDomain::Surface) {
		scenePreview_.GetCompileDeadline() = 0.0;
		return;
	}
	if (!scenePreview_.GetTargetEntityUUID()) {
		scenePreview_.GetCompileDeadline() = 0.0;
		return;
	}
	if (!editSession_.NeedsCompile()) {
		scenePreview_.GetCompileDeadline() = 0.0;
		if (!scenePreview_.IsMaterialApplied()) {
			ApplyPreviewMaterial(context);
		}
		return;
	}
	if (ImGui::IsAnyItemActive()) {
		scenePreview_.GetCompileDeadline() = 0.0;
		return;
	}

	const double now = ImGui::GetTime();
	if (scenePreview_.GetCompileDeadline() <= 0.0) {
		scenePreview_.GetCompileDeadline() = now + 0.25;
		return;
	}
	if (now < scenePreview_.GetCompileDeadline()) {
		return;
	}

	scenePreview_.GetCompileDeadline() = 0.0;
	if (SaveAndCompile(context)) {
		ApplyPreviewMaterial(context);
	}
}

bool Engine::ShaderGraphEditorTool::LoadAppearanceSettings() {

	return ShaderGraphAppearance::LoadAppearanceSettings(appearanceSetting_);
}

void Engine::ShaderGraphEditorTool::SaveAppearanceSettings() const {

	ShaderGraphAppearance::SaveAppearanceSettings(appearanceSetting_);
}

void Engine::ShaderGraphEditorTool::ApplyAppearanceSettings() {

	ShaderGraphAppearance::ApplyAppearanceSettings(appearanceSetting_);
}

void Engine::ShaderGraphEditorTool::RestoreDefaultAppearance() {

	ShaderGraphAppearance::RestoreDefaultAppearance(appearanceSetting_);
}

void Engine::ShaderGraphEditorTool::ClampAppearanceSettings() {

	ShaderGraphAppearance::ClampAppearanceSettings(appearanceSetting_);
}

void Engine::ShaderGraphEditorTool::CaptureNodePositions() {

	if (!nodeEditor_) {
		return;
	}
	ed::SetCurrentEditor(nodeEditor_);
	for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		const ImVec2 position =
			ed::GetNodePosition(
				ed::NodeId(ToNodeEditorID(node.id)));
		node.position =
			Vector2(position.x, position.y);
	}
	for (ShaderGraphGroup& group : editSession_.GetDraft().groups) {
		const ed::NodeId groupID(ToNodeEditorID(group.id));
		const ImVec2 position =
			ed::GetNodePosition(groupID);
		group.position =
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
	nodeValuePopupNode_ = UUID{};
	nodeValuePopupKind_ = NodeValuePopupKind::None;
	nodeValuePopupAnchor_ = Vector2{};
	nodeValuePopupWidth_ = 0.0f;
	nodeValuePopupViewportID_ = 0;
	requestNodeValuePopup_ = false;
}

void Engine::ShaderGraphEditorTool::RemoveNode(UUID nodeID) {

	if (nodeID == editSession_.GetDraft().vertexOutputNode) {
		editSession_.GetDraft().vertexOutputNode = UUID{};
	}

	std::erase_if(
		editSession_.GetDraft().nodes,
		[&](const ShaderGraphNode& node) {
			return node.id == nodeID;
		});
	std::erase_if(
		editSession_.GetDraft().links,
		[&](const ShaderGraphLink& link) {
			return link.inputNode == nodeID ||
				link.outputNode == nodeID;
		});
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::RemoveGroup(UUID groupID) {

	if (editingGroup_ == groupID) {
		editingGroup_ = UUID{};
		requestGroupNameFocus_ = false;
	}
	for (ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		if (node.groupID == groupID) {
			node.groupID = UUID{};
		}
	}
	std::erase_if(
		editSession_.GetDraft().groups,
		[&](const ShaderGraphGroup& group) {
			return group.id == groupID;
		});
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::DuplicateNode(UUID nodeID) {

	const auto found = std::find_if(
		editSession_.GetDraft().nodes.begin(),
		editSession_.GetDraft().nodes.end(),
		[&](const ShaderGraphNode& node) {
			return node.id == nodeID;
		});
	if (found == editSession_.GetDraft().nodes.end() ||
		nodeID == editSession_.GetDraft().outputNode ||
		nodeID == editSession_.GetDraft().vertexOutputNode) {
		return;
	}

	const ImVec2 sourcePosition =
		ed::GetNodePosition(
			ed::NodeId(ToNodeEditorID(nodeID)));
	ShaderGraphNode duplicate = *found;
	duplicate.id = UUID::New();
	duplicate.position = Vector2(
		sourcePosition.x + kDuplicateOffset,
		sourcePosition.y + kDuplicateOffset);
	for (ShaderGraphPort& port : duplicate.inputPorts) {
		port.id = UUID::New();
	}
	for (ShaderGraphPort& port : duplicate.outputPorts) {
		port.id = UUID::New();
	}
	const UUID duplicateID = duplicate.id;
	editSession_.GetDraft().nodes.emplace_back(std::move(duplicate));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(duplicateID)),
		ImVec2(
			sourcePosition.x + kDuplicateOffset,
			sourcePosition.y + kDuplicateOffset));

	ed::ClearSelection();
	ed::SelectNode(
		ed::NodeId(ToNodeEditorID(duplicateID)));
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::DuplicateGroup(UUID groupID) {

	const auto found = std::find_if(
		editSession_.GetDraft().groups.begin(),
		editSession_.GetDraft().groups.end(),
		[&](const ShaderGraphGroup& group) {
			return group.id == groupID;
		});
	if (found == editSession_.GetDraft().groups.end()) {
		return;
	}

	const ShaderGraphGroup sourceGroup = *found;
	const ed::NodeId sourceGroupID(ToNodeEditorID(groupID));
	const ImVec2 sourcePosition =
		ed::GetNodePosition(sourceGroupID);
	const ImVec2 sourceSize =
		ed::GetNodeSize(sourceGroupID);
	const float sourceWidth =
		0.0f < sourceSize.x ?
		sourceSize.x : sourceGroup.size.x;
	const ImVec2 duplicateOffset(sourceWidth + kDuplicateGroupSpacing, 0.0f);
	ShaderGraphGroup duplicateGroup = sourceGroup;
	duplicateGroup.id = UUID::New();
	duplicateGroup.name += " コピー";
	duplicateGroup.position = Vector2(
		sourcePosition.x + duplicateOffset.x,
		sourcePosition.y + duplicateOffset.y);
	duplicateGroup.size = Vector2(
		0.0f < sourceSize.x ?
		sourceSize.x : sourceGroup.size.x,
		0.0f < sourceSize.y ?
		sourceSize.y : sourceGroup.size.y);
	const UUID newGroupID = duplicateGroup.id;

	// グループ内ノードと内部リンクだけを複製
	std::unordered_map<uint64_t, UUID> duplicateNodeIDs{};
	const size_t sourceNodeCount = editSession_.GetDraft().nodes.size();
	for (size_t index = 0;
		index < sourceNodeCount; ++index) {

		const ShaderGraphNode& source =
			editSession_.GetDraft().nodes[index];
		if (source.groupID != groupID ||
			source.id == editSession_.GetDraft().outputNode ||
			source.id == editSession_.GetDraft().vertexOutputNode) {

			continue;
		}

		const ed::NodeId sourceNodeID(ToNodeEditorID(source.id));
		const ImVec2 nodePosition =
			ed::GetNodePosition(sourceNodeID);
		ShaderGraphNode duplicate = source;
		duplicate.id = UUID::New();
		duplicate.groupID = newGroupID;
		duplicate.position = Vector2(
			nodePosition.x + duplicateOffset.x,
			nodePosition.y + duplicateOffset.y);
		for (ShaderGraphPort& port : duplicate.inputPorts) {
			port.id = UUID::New();
		}
		for (ShaderGraphPort& port : duplicate.outputPorts) {
			port.id = UUID::New();
		}
		duplicateNodeIDs.emplace(
			source.id.value, duplicate.id);
		const UUID duplicateID = duplicate.id;
		editSession_.GetDraft().nodes.emplace_back(std::move(duplicate));
		ed::SetNodePosition(
			ed::NodeId(ToNodeEditorID(duplicateID)),
			ImVec2(
				nodePosition.x + duplicateOffset.x,
				nodePosition.y + duplicateOffset.y));
	}

	const size_t sourceLinkCount = editSession_.GetDraft().links.size();
	for (size_t index = 0;
		index < sourceLinkCount; ++index) {

		const ShaderGraphLink& source =
			editSession_.GetDraft().links[index];
		const auto output = duplicateNodeIDs.find(
			source.outputNode.value);
		const auto input = duplicateNodeIDs.find(
			source.inputNode.value);
		if (output == duplicateNodeIDs.end() ||
			input == duplicateNodeIDs.end()) {
			continue;
		}

		ShaderGraphLink duplicate = source;
		duplicate.id = UUID::New();
		duplicate.outputNode = output->second;
		duplicate.inputNode = input->second;
		editSession_.GetDraft().links.emplace_back(std::move(duplicate));
	}

	editSession_.GetDraft().groups.emplace_back(std::move(duplicateGroup));

	const ed::NodeId newEditorGroupID(ToNodeEditorID(newGroupID));
	ed::SetNodePosition(
		newEditorGroupID, ImVec2(sourcePosition.x + duplicateOffset.x, sourcePosition.y + duplicateOffset.y));
	ed::SetGroupSize(
		newEditorGroupID, ImVec2(editSession_.GetDraft().groups.back().size.x, editSession_.GetDraft().groups.back().size.y));
	ed::ClearSelection();
	ed::SelectNode(newEditorGroupID);
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::RemoveParameter(uint32_t index) {

	if (editSession_.GetDraft().parameters.size() <= index) {
		return;
	}
	const UUID parameterID =
		editSession_.GetDraft().parameters[index].id;
	std::vector<UUID> nodes;
	for (const ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		if (node.kind == ShaderGraphNodeKind::Parameter &&
			node.parameterID == parameterID) {

			nodes.emplace_back(node.id);
		}
	}
	for (UUID nodeID : nodes) {
		RemoveNode(nodeID);
	}
	editSession_.GetDraft().parameters.erase(
		editSession_.GetDraft().parameters.begin() + index);
	selectedParameter_ = -1;
	editSession_.MarkDirty();
}

std::vector<Engine::UUID>
Engine::ShaderGraphEditorTool::GetSelectedGraphNodes() const {

	const int32_t selectedCount =
		ed::GetSelectedNodes(nullptr, 0);
	if (selectedCount <= 0) {
		return {};
	}

	std::vector<ed::NodeId> selectedIDs(static_cast<size_t>(selectedCount));
	const int32_t writtenCount =
		ed::GetSelectedNodes(
			selectedIDs.data(),
			selectedCount);

	std::vector<UUID> nodes;
	nodes.reserve(static_cast<size_t>(writtenCount));
	for (int32_t index = 0;
		index < writtenCount; ++index) {

		const UUID nodeID{
			static_cast<uint64_t>(
				selectedIDs[index].Get())
		};
		const auto found = std::find_if(
			editSession_.GetDraft().nodes.begin(),
			editSession_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& node) {
				return node.id == nodeID;
			});
		if (found != editSession_.GetDraft().nodes.end()) {
			nodes.emplace_back(nodeID);
		}
	}
	return nodes;
}

void Engine::ShaderGraphEditorTool::GroupSelectedNodes() {

	const std::vector<UUID> selectedNodes =
		GetSelectedGraphNodes();
	if (selectedNodes.size() < 2) {
		return;
	}

	Vector2 minimum{
		(std::numeric_limits<float>::max)(),
		(std::numeric_limits<float>::max)(),
	};
	Vector2 maximum{
		(std::numeric_limits<float>::lowest)(),
		(std::numeric_limits<float>::lowest)(),
	};
	for (UUID nodeID : selectedNodes) {
		const ed::NodeId editorNodeID(ToNodeEditorID(nodeID));
		const ImVec2 position =
			ed::GetNodePosition(editorNodeID);
		const ImVec2 size =
			ed::GetNodeSize(editorNodeID);
		minimum.x =
			(std::min)(minimum.x, position.x);
		minimum.y =
			(std::min)(minimum.y, position.y);
		maximum.x =
			(std::max)(maximum.x, position.x + size.x);
		maximum.y =
			(std::max)(maximum.y, position.y + size.y);

		const auto node = std::find_if(
			editSession_.GetDraft().nodes.begin(),
			editSession_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) {
				return value.id == nodeID;
			});
		if (node != editSession_.GetDraft().nodes.end()) {
			node->position =
				Vector2(position.x, position.y);
		}
	}

	ShaderGraphGroup group{
		.id = UUID::New(),
		.name = "グループ " +
			std::to_string(editSession_.GetDraft().groups.size() + 1),
		.position = Vector2(
			minimum.x - kGroupHorizontalPadding,
			minimum.y - kGroupTopPadding),
		.size = Vector2(
			maximum.x - minimum.x +
				kGroupHorizontalPadding * 2.0f,
			maximum.y - minimum.y +
				kGroupTopPadding +
				kGroupBottomPadding),
	};
	const ed::NodeId groupID(ToNodeEditorID(group.id));
	for (UUID nodeID : selectedNodes) {
		const auto node = std::find_if(
			editSession_.GetDraft().nodes.begin(),
			editSession_.GetDraft().nodes.end(),
			[&](const ShaderGraphNode& value) {
				return value.id == nodeID;
			});
		if (node != editSession_.GetDraft().nodes.end()) {
			node->groupID = group.id;
		}
	}
	ed::SetNodePosition(
		groupID, ImVec2(group.position.x, group.position.y));
	ed::SetGroupSize(
		groupID, ImVec2(group.size.x, group.size.y));
	editSession_.GetDraft().groups.emplace_back(std::move(group));

	ed::ClearSelection();
	ed::SelectNode(groupID);
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::CopySelection() {

	const std::vector<UUID> selected = GetSelectedGraphNodes();
	if (selected.empty()) {
		return;
	}
	std::unordered_set<uint64_t> selectedIDs;
	for (UUID id : selected) {
		selectedIDs.insert(id.value);
	}
	ShaderGraphAsset fragment{};
	fragment.name = "Clipboard";
	fragment.target = editSession_.GetDraft().target;
	for (const ShaderGraphNode& node : editSession_.GetDraft().nodes) {
		if (!selectedIDs.contains(node.id.value) ||
			node.id == editSession_.GetDraft().outputNode ||
			node.id == editSession_.GetDraft().vertexOutputNode) {
			continue;
		}
		fragment.nodes.emplace_back(node);
	}
	if (fragment.nodes.empty()) {
		return;
	}
	fragment.outputNode = fragment.nodes.front().id;
	for (const ShaderGraphLink& link : editSession_.GetDraft().links) {
		if (selectedIDs.contains(link.outputNode.value) &&
			selectedIDs.contains(link.inputNode.value)) {
			fragment.links.emplace_back(link);
		}
	}
	const std::string text =
		"NEM_SHADER_GRAPH_CLIPBOARD\n" + ToJson(fragment).dump();
	ImGui::SetClipboardText(text.c_str());
	editSession_.GetStatusMessage() = "選択ノードをコピーしました";
}

void Engine::ShaderGraphEditorTool::PasteSelection() {

	const char* clipboard = ImGui::GetClipboardText();
	if (!clipboard) {
		return;
	}
	constexpr std::string_view kHeader =
		"NEM_SHADER_GRAPH_CLIPBOARD\n";
	const std::string_view text(clipboard);
	if (!text.starts_with(kHeader)) {
		return;
	}

	const nlohmann::json data = nlohmann::json::parse(
		text.substr(kHeader.size()), nullptr, false);
	ShaderGraphAsset fragment{};
	if (data.is_discarded() || !FromJson(data, fragment)) {
		editSession_.GetStatusMessage() = "コピーしたノードを読み込めませんでした";
		return;
	}

	std::unordered_map<uint64_t, UUID> idMap;
	ed::ClearSelection();
	for (ShaderGraphNode& node : fragment.nodes) {
		const UUID sourceID = node.id;
		node.id = UUID::New();
		node.groupID = UUID{};
		idMap[sourceID.value] = node.id;
		node.position.x += 32.0f;
		node.position.y += 32.0f;
		for (ShaderGraphPort& port : node.inputPorts) {
			port.id = UUID::New();
		}
		for (ShaderGraphPort& port : node.outputPorts) {
			port.id = UUID::New();
		}
		const UUID nodeID = node.id;
		const Vector2 position = node.position;
		editSession_.GetDraft().nodes.emplace_back(std::move(node));
		ed::SetNodePosition(
			ed::NodeId(ToNodeEditorID(nodeID)),
			ImVec2(position.x, position.y));
		ed::SelectNode(
			ed::NodeId(ToNodeEditorID(nodeID)), true);
	}
	for (ShaderGraphLink& link : fragment.links) {
		const auto source = idMap.find(link.outputNode.value);
		const auto destination = idMap.find(link.inputNode.value);
		if (source == idMap.end() || destination == idMap.end()) {
			continue;
		}
		link.id = UUID::New();
		link.outputNode = source->second;
		link.inputNode = destination->second;
		editSession_.GetDraft().links.emplace_back(std::move(link));
	}
	editSession_.MarkDirty();
	CommitGraphHistory();
	editSession_.GetStatusMessage() = "ノードを貼り付けました";
}

void Engine::ShaderGraphEditorTool::AddNode(ShaderGraphNodeKind kind, Vector2 position) {

	if (kind == ShaderGraphNodeKind::VertexOutput &&
		editSession_.GetDraft().vertexOutputNode) {

		return;
	}

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
	} else if (kind == ShaderGraphNodeKind::SamplerState) {

		node.sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.sampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.sampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.previewExpanded = false;
	} else if (kind == ShaderGraphNodeKind::CustomFunction) {
		const std::string functionName =
			"CustomFunction_" + ToString(node.id);
		node.functionName = functionName;
		node.inputPorts.emplace_back(ShaderGraphPort{
			.id = UUID::New(),
			.name = "Input",
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(
				ShaderGraphValueType::Float),
			});
		node.outputPorts.emplace_back(ShaderGraphPort{
			.id = UUID::New(),
			.name = "Output",
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(
				ShaderGraphValueType::Float),
			});
		node.functionBody =
			"void " + functionName +
			"(float Input, out float Output) {\n"
			"\tOutput = Input;\n"
			"}";
	}
	const UUID nodeID = node.id;
	editSession_.GetDraft().nodes.emplace_back(std::move(node));
	if (kind == ShaderGraphNodeKind::VertexOutput) {
		editSession_.GetDraft().vertexOutputNode = nodeID;
	}
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::AddConstantNode(ShaderGraphValueType type, Vector2 position) {

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Constant,
		.valueType = type,
		.value = DefaultValueForGraphType(type),
		.position = position,
		.previewExpanded = false,
	};
	const UUID nodeID = node.id;
	editSession_.GetDraft().nodes.emplace_back(std::move(node));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::AddParameterNode(UUID parameterID, Vector2 position) {

	const auto parameter = std::find_if(
		editSession_.GetDraft().parameters.begin(),
		editSession_.GetDraft().parameters.end(),
		[&](const ShaderGraphParameter& value) {
			return value.id == parameterID;
		});
	if (parameter == editSession_.GetDraft().parameters.end()) {
		return;
	}
	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Parameter,
		.parameterID = parameterID,
		.valueType = parameter->type,
		.position = position,
		.previewExpanded = false,
	};
	const UUID nodeID = node.id;
	editSession_.GetDraft().nodes.emplace_back(std::move(node));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::AddKeywordNode(UUID keywordID, Vector2 position) {

	const auto keyword = std::find_if(
		editSession_.GetDraft().keywords.begin(), editSession_.GetDraft().keywords.end(),
		[&](const ShaderGraphKeyword& value) {
			return value.id == keywordID;
		});
	if (keyword == editSession_.GetDraft().keywords.end()) {
		return;
	}

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Keyword,
		.valueType = keyword->type == ShaderGraphKeywordType::Boolean ?
			ShaderGraphValueType::Boolean : ShaderGraphValueType::Integer,
		.position = position,
		.keywordID = keywordID,
		.previewExpanded = false,
	};
	const UUID nodeID = node.id;
	editSession_.GetDraft().nodes.emplace_back(std::move(node));
	ed::SetNodePosition(
		ed::NodeId(ToNodeEditorID(nodeID)),
		ImVec2(position.x, position.y));
	editSession_.MarkDirty();
}

void Engine::ShaderGraphEditorTool::UndoGraph() {

	CommitGraphHistory();
	if (!editSession_.Undo()) {
		return;
	}
	nodePreviews_.InvalidateNodePreviews();

	restoreNodePositions_ = true;
	editingGroup_ = UUID{};
	contextNode_ = UUID{};
	selectedParameter_ = -1;
	ResetNodeEditor();
	editSession_.GetStatusMessage() = "編集を元に戻しました";
}

void Engine::ShaderGraphEditorTool::RedoGraph() {

	if (!editSession_.Redo()) {
		return;
	}
	nodePreviews_.InvalidateNodePreviews();

	restoreNodePositions_ = true;
	editingGroup_ = UUID{};
	contextNode_ = UUID{};
	selectedParameter_ = -1;
	ResetNodeEditor();
	editSession_.GetStatusMessage() = "編集をやり直しました";
}

void Engine::ShaderGraphEditorTool::CommitGraphHistory() {

	CaptureNodePositions();
	editSession_.Commit();
}

bool Engine::ShaderGraphEditorTool::LoadGraph(const EditorToolContext& context, AssetID assetID) {

	RestorePreviewMaterial(context);
	const bool loaded = editSession_.Load(context, assetID);
	if (!loaded) {
		if (!context.toolContext.assetDatabase || !assetID) {
			nodePreviews_.ClearNodePreviews();
			ResetNodeEditor();
		}
		return false;
	}
	nodePreviews_.ClearNodePreviews();
	selectedParameter_ = -1;
	ResetNodeEditor();
	restoreNodePositions_ = true;
	return true;
}

bool Engine::ShaderGraphEditorTool::SaveAndCompile(const EditorToolContext& context) {

	const auto graphPath = editSession_.ResolveCompilePath(context);
	if (graphPath.empty()) {
		return false;
	}
	CaptureNodePositions();
	return editSession_.SaveAndCompile(context, graphPath);
}
