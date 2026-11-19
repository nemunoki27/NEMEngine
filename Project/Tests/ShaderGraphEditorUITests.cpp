#include "ShaderGraphEditorUITests.h"
#include "ImGuiTestFixture.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphCanvasContext.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphCanvasMenu.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphGroupEditor.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphNodeTransfer.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphNodeDrawer.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphEditOperations.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <cmath>
#include <iostream>
#include <unordered_set>
// imgui
#include <imgui_internal.h>
#include <imgui_node_editor.h>

using namespace Engine;
namespace ed = ax::NodeEditor;

namespace {

	// 検証画像の保存先を取得する
	std::filesystem::path ImagePath(const char* name) {

		return RuntimePaths::GetEngineProjectRoot().parent_path() / "LocalWork/Verification/ShaderGraphVerification/UI" / name;
	}

	// OSのWindowに依存しない描画領域を開く
	void BeginHost(ImVec2 position = ImVec2(40, 40)) {

		ImGui::SetNextWindowPos(position);
		ImGui::SetNextWindowSize(ImVec2(1300, 900));
		ImGui::Begin("ShaderGraphUI", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
	}

	// 型名と分類を全登録ノードで確認する
	bool CheckLabels() {

		const std::array types{ShaderGraphValueType::Float, ShaderGraphValueType::Float2,
			ShaderGraphValueType::Float3, ShaderGraphValueType::Float4, ShaderGraphValueType::Color};
		const std::array names{"float", "Vector2", "Vector3", "Vector4", "Color"};
		for (size_t index = 0; index < types.size(); ++index) {
			const char* name = ImGuiUtility::GetEnumDisplayName(magic_enum::enum_type_name<ShaderGraphValueType>(),
				EnumAdapter<ShaderGraphValueType>::ToString(types[index]));
			if (std::string_view(name) != names[index]) return false;
		}
		std::unordered_set<std::string_view> categories;
		for (const auto& descriptor : ShaderGraphNodeRegistry::GetDescriptors()) {
			for (unsigned char value : descriptor.name) if (127 < value) return false;
			for (unsigned char value : descriptor.category) if (127 < value) return false;
			if (descriptor.category == "Math") return false;
			if (descriptor.category.starts_with("Math/")) categories.insert(descriptor.category.substr(5));
		}
		for (const auto category : {"Basic", "Advanced", "Trigonometry", "Interpolation", "Range", "Rounding", "Wave"}) {
			if (!categories.erase(category)) return false;
		}
		const MaterialParameterValue vector{.value = Vector4(.2f, .4f, .6f, .8f)};
		const MaterialParameterValue color{.value = Color4(.2f, .4f, .6f, .8f)};
		const auto converted = ShaderGraphEditOperations::GetColorValue(vector);
		return categories.empty() && converted.r == .2f && converted.g == .4f && converted.b == .6f &&
			converted.a == .8f && ShaderGraphEditOperations::GetVector4Value(color) == Vector4(.2f, .4f, .6f, .8f);
	}

	// 実際のNode描画から数値の入力幅を調べる
	bool CheckNodeWidths(NEMTests::ImGuiTestFixture& fixture) {

		ShaderGraphEditSession session;
		ShaderGraphAppearanceSetting settings;
		ShaderGraphNodePreviews previews;
		ShaderGraphNodeDrawer drawer(session, settings, previews);
		ShaderGraphCanvasContext canvas;
		canvas.EnsureCreated();
		ed::SetCurrentEditor(canvas.GetEditor());
		const std::array types{ShaderGraphValueType::Float2, ShaderGraphValueType::Float3, ShaderGraphValueType::Float4};
		for (size_t index = 0; index < types.size(); ++index) {
			session.GetDraft().nodes.emplace_back(ShaderGraphNode{
				.id = Engine::UUID{index + 1}, .kind = ShaderGraphNodeKind::Constant, .valueType = types[index],
				.value = ShaderGraphEditOperations::DefaultValueForGraphType(types[index]), .previewExpanded = false,
			});
		}
		for (float scale : {1.0f, 1.5f, 2.0f}) {
			settings.nodeTextScale = scale;
			for (int frame = 0; frame < 3; ++frame) {
				bool valid = true;
				fixture.Begin();
				BeginHost();
				ed::Begin("NodeWidths");
				for (size_t index = 0; index < types.size(); ++index) {
					auto& node = session.GetDraft().nodes[index];
					ed::SetNodePosition(ed::NodeId(node.id.value), ImVec2(20, 30 + static_cast<float>(index) * 230));
					drawer.DrawNode(node);
					if (frame == 2) {
						ImGui::PushID(static_cast<int>(node.id.value));
						const auto* table = GImGui->Tables.GetByKey(ImGui::GetID("##MyGUI_RowTable_Public_値"));
						ImGui::PopID();
						if (!table) { valid = false; continue; }
						const float count = static_cast<float>(index + 2);
						ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
						const auto& style = ImGui::GetStyle();
						const float required = ImGui::CalcTextSize("-0.000").x + style.FramePadding.x * 2 +
							ImGui::CalcTextSize("W").x + style.ItemInnerSpacing.x;
						ImGui::PopFont();
						const float actual = (table->Columns[1].WidthGiven - style.CellPadding.x * 2 -
							style.ItemInnerSpacing.x * (count - 1)) / count;
						if (actual + 1 < required) {
							std::cerr << "Node field width: scale=" << scale << " count=" << count <<
								" actual=" << actual << " required=" << required << '\n';
							valid = false;
						}
					}
				}
				ed::Suspend();
				drawer.DrawPopup();
				ed::Resume();
				ed::End();
				ImGui::End();
				const char* name = scale == 1 ? "node-values-100.png" : scale == 1.5f ? "node-values-150.png" : "node-values-200.png";
				if (!fixture.End(frame == 2 ? ImagePath(name) : std::filesystem::path{})) return false;
				if (!valid) return false;
			}
		}
		return true;
	}

	// Zoom後も色のTooltipを画面上のマウス位置へ表示する
	bool CheckColorTooltip(NEMTests::ImGuiTestFixture& fixture) {

		ShaderGraphCanvasContext canvas;
		canvas.EnsureCreated();
		ed::SetCurrentEditor(canvas.GetEditor());
		ShaderGraphEditSession session;
		session.GetDraft().nodes.emplace_back(ShaderGraphNode{
			.id = Engine::UUID{41}, .kind = ShaderGraphNodeKind::Constant, .valueType = ShaderGraphValueType::Color,
			.value = {.value = Vector4(.2f, .4f, .8f, .65f)}, .previewExpanded = false,
		});
		ShaderGraphNodeValueEditor editor;
		ImVec2 point(400, 300);
		ImVec2 pickerPoint;
		float previousZoom = 0;
		for (int pass = 0; pass < 3; ++pass) {
			bool visible = false;
			const int frames = pass == 2 ? 8 : 60;
			for (int frame = 0; frame < frames; ++frame) {
				bool valid = true;
				auto& io = ImGui::GetIO();
				const ImVec2 mouse = pass == 2 && 2 < frame ? pickerPoint : point;
				io.AddMousePosEvent(mouse.x, mouse.y);
				if (pass == 1 && frame < 4) io.AddMouseWheelEvent(0, -1);
				if (pass == 2 && (frame == 1 || frame == 4)) io.AddMouseButtonEvent(0, true);
				if (pass == 2 && (frame == 2 || frame == 5)) io.AddMouseButtonEvent(0, false);
				fixture.Begin();
				BeginHost(pass == 0 ? ImVec2(40, 40) : ImVec2(230, 80));
				ed::Begin("ColorTooltip");
				ed::SetNodePosition(ed::NodeId(41), ImVec2(60, 80));
				ed::BeginNode(ed::NodeId(41));
				ImGui::PushID(41);
				const auto tableID = ImGui::GetID("##MyGUI_RowTable_Public_値");
				editor.DrawValue(session, session.GetDraft().nodes.front(), 380);
				const auto* table = GImGui->Tables.GetByKey(tableID);
				const auto& style = ImGui::GetStyle();
				if (table) {
					point = ed::CanvasToScreen(ImVec2(table->Columns[1].WorkMinX + ImGui::GetFrameHeight() * .5f,
						table->OuterRect.Min.y + style.CellPadding.y + ImGui::GetFrameHeight() * .5f));
				} else valid = false;
				ImGui::PopID();
				ed::EndNode();
				ed::Suspend();
				editor.DrawPopup(session);
				if (pass == 2 && 1 < frame && !GImGui->OpenPopupStack.empty()) {
					if (auto* popup = GImGui->OpenPopupStack.back().Window) {
						pickerPoint = popup->Pos + ImGui::GetStyle().WindowPadding + ImVec2(50, 50);
					}
				}
				for (auto* window : GImGui->Windows) {
					if (!window->Active || !(window->Flags & ImGuiWindowFlags_Tooltip)) continue;
					const auto delta = window->Pos - ImGui::GetMousePos();
					if (150 < std::abs(delta.x) || 150 < std::abs(delta.y)) valid = false;
					visible = true;
				}
				ed::Resume();
				const float zoom = ed::GetCurrentZoom();
				ed::End();
				ImGui::End();
				const char* name = pass == 0 ? "tooltip.png" : pass == 1 ? "tooltip-zoom.png" : "color-picker.png";
				if (!fixture.End(frame == frames - 1 ? ImagePath(name) : std::filesystem::path{})) return false;
				if (!valid) return false;
				if (pass < 2 && frame == frames - 1) {
					if (!visible || (pass == 1 && previousZoom == zoom)) return false;
					previousZoom = zoom;
				}
			}
			if (pass < 2 && session.IsDirty()) return false;
		}
		const auto color = ShaderGraphEditOperations::GetColorValue(session.GetDraft().nodes.front().value);
		ImGui::ClosePopupsOverWindow(nullptr, false);
		return session.IsDirty() && (color.r != .2f || color.g != .4f || color.b != .8f);
	}

	// 実際の追加MenuからMathの下位分類を開いて検索する
	bool CheckMenu(NEMTests::ImGuiTestFixture& fixture) {

		ShaderGraphEditSession session;
		ShaderGraphCanvasContext canvas;
		canvas.EnsureCreated();
		ed::SetCurrentEditor(canvas.GetEditor());
		ShaderGraphAppearanceSetting settings;
		ShaderGraphGroupEditor groups(session, settings);
		ShaderGraphNodeTransfer transfer(session);
		ShaderGraphCanvasMenu menu(session, canvas, groups, transfer);
		ImGuiWindow* root = nullptr;
		ImGuiWindow* math = nullptr;
		bool opened = false;
		for (int frame = 0; frame < 7; ++frame) {
			bool valid = true;
			fixture.Begin();
			if (frame == 0) ImGui::SetNextWindowFocus();
			BeginHost();
			ed::Begin("Menu");
			ed::Suspend();
			if (frame == 0) { ImGui::SetNextWindowPos(ImVec2(80, 100)); menu.OpenCreate({100, 100}); }
			if (frame == 2 || frame == 4) {
				auto* target = frame == 2 ? root : math;
				if (target) {
					GImGui->NavWindow = target;
					GImGui->NavId = GImGui->NavActivateId = target->GetID(frame == 2 ? "Math" : "Trigonometry");
					GImGui->NavMoveDir = ImGuiDir_Right;
				} else valid = false;
			}
			menu.Draw({});
			if (!GImGui->OpenPopupStack.empty()) root = GImGui->OpenPopupStack[0].Window;
			if (1 < GImGui->OpenPopupStack.Size) math = GImGui->OpenPopupStack[1].Window;
			if (2 < GImGui->OpenPopupStack.Size) opened = true;
			ed::Resume();
			ed::End();
			ImGui::End();
			if (!fixture.End(frame == 6 || !valid ? ImagePath("math-menu.png") : std::filesystem::path{})) return false;
			if (!valid) return false;
		}
		if (!opened || !root) return false;
		ImGui::ClosePopupToLevel(1, true);
		const float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
		const ImVec2 searchPoint(root->Pos.x + 100, root->Pos.y + ImGui::GetStyle().WindowPadding.y +
			rowHeight * 2 + ImGui::GetStyle().ItemSpacing.y + 10);
		for (int frame = 0; frame < 5; ++frame) {
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(searchPoint.x, searchPoint.y);
			if (frame == 1) io.AddMouseButtonEvent(0, true);
			if (frame == 2) io.AddMouseButtonEvent(0, false);
			if (frame == 3) io.AddInputCharactersUTF8("sine");
			fixture.Begin();
			BeginHost();
			ed::Begin("Menu");
			ed::Suspend();
			menu.Draw({});
			ed::Resume();
			ed::End();
			ImGui::End();
			if (!fixture.End(frame == 4 ? ImagePath("node-search.png") : std::filesystem::path{})) return false;
		}
		const auto* input = ImGui::GetInputTextState(root->GetID("##NodeSearch"));
		const bool searched = input && std::string_view(input->TextA.Data) == "sine";
		ImGui::ClosePopupsOverWindow(nullptr, false);
		return searched;
	}
}

bool NEMTests::TestShaderGraphEditorUI() {

	if (!RunTest("ShaderGraphEditorLabels", CheckLabels)) return false;
	ImGuiTestFixture fixture;
	return RunTest("ShaderGraphNodeWidths", [&] { return CheckNodeWidths(fixture); }) &&
		RunTest("ShaderGraphColorTooltip", [&] { return CheckColorTooltip(fixture); }) &&
		RunTest("ShaderGraphCreationMenu", [&] { return CheckMenu(fixture); }) && fixture.CheckMessages();
}
