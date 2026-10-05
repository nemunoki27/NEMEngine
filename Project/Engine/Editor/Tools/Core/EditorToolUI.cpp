#include "EditorToolUI.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <algorithm>

//============================================================================
//	EditorToolUI classMethods
//============================================================================

namespace {

	bool CanDrawToolUI(const Engine::EditorPanelContext& context) {
		return context.editorContext && context.layoutState &&
			!context.layoutState->hidePanels;
	}

	Engine::ToolContext MakeToolContext(const Engine::EditorPanelContext& context) {
		Engine::ToolContext result{};
		if (!context.editorContext) {
			return result;
		}

		const auto& editor = *context.editorContext;
		result.world = editor.activeWorld;
		result.assetDatabase = editor.assetDatabase;
		result.sceneInstances = editor.sceneInstances;
		result.activeSceneHeader = editor.activeSceneHeader;
		result.activeSceneAsset = editor.activeSceneAsset;
		result.activeSceneInstanceID = editor.activeSceneInstanceID;
		result.activeScenePath = editor.activeScenePath;
		result.isPlaying = editor.isPlaying;
		result.canEditScene = context.CanEditScene();
		return result;
	}

	std::vector<Engine::IEditorTool*> CollectEditorTools() {
		const auto registered = Engine::ToolRegistry::GetInstance().GetTools();
		std::vector<Engine::IEditorTool*> result;
		result.reserve(registered.size());

		// ToolRegistryのcategory / order / id順をそのまま維持する。
		for (Engine::ITool* tool : registered) {
			if (auto* editorTool = dynamic_cast<Engine::IEditorTool*>(tool)) {
				result.push_back(editorTool);
			}
		}
		return result;
	}

	const char* OwnerLabel(Engine::ToolOwner owner) {
		return owner == Engine::ToolOwner::Game ? "Game" : "Engine";
	}

	void DrawToolTooltip(const Engine::ToolDescriptor& desc, bool enabled) {
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			return;
		}
		if (!ImGui::BeginTooltip()) {
			return;
		}

		ImGui::SetWindowFontScale(0.8f);
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
		ImGui::TextDisabled("[%s]", OwnerLabel(desc.owner));
		if (!desc.description.empty()) {
			ImGui::TextUnformatted(desc.description.c_str());
		}
		if (!enabled) {
			ImGui::Separator();
			ImGui::TextUnformatted("現在のモードではこのツールを開けません。");
		}
		ImGui::PopTextWrapPos();
		ImGui::SetWindowFontScale(1.0f);
		ImGui::EndTooltip();
	}

	// C++20で使えるスコープガード。描画中の借用参照を必ず解除する。
	class EditorToolFrameScope final {
	public:
		EditorToolFrameScope(Engine::IEditorTool& tool,
			const Engine::EditorToolContext& context) : tool_(tool) {
			tool_.BeginEditorToolFrame(context);
		}

		~EditorToolFrameScope() {
			tool_.EndEditorToolFrame();
		}

		EditorToolFrameScope(const EditorToolFrameScope&) = delete;
		EditorToolFrameScope& operator=(const EditorToolFrameScope&) = delete;

	private:
		Engine::IEditorTool& tool_;
	};
}

void Engine::EditorToolUI::DrawMenu(const EditorPanelContext& context) {
	if (!CanDrawToolUI(context) || !ImGui::BeginMenu("ツール")) {
		return;
	}

	ImGui::SetWindowFontScale(0.8f);

	const auto tools = CollectEditorTools();
	const ToolContext toolContext = MakeToolContext(context);
	std::string openToolID;

	if (tools.empty()) {
		ImGui::MenuItem("登録されたツールはありません", nullptr, false, false);
	}

	std::size_t first = 0;
	while (first < tools.size()) {
		const std::string& category = tools[first]->GetDescriptor().category;
		std::size_t last = first + 1;
		while (last < tools.size() &&
			tools[last]->GetDescriptor().category == category) {
			++last;
		}

		// 空カテゴリと、表示名が「その他」のカテゴリも識別できる。
		ImGui::PushID(category.c_str());
		if (ImGui::BeginMenu(category.empty() ? "その他" : category.c_str())) {
			ImGui::SetWindowFontScale(0.8f);
			for (std::size_t index = first; index < last; ++index) {
				IEditorTool& tool = *tools[index];
				const ToolDescriptor& desc = tool.GetDescriptor();
				const bool enabled = tool.IsEnabled(toolContext);
				const char* label = desc.name.empty() ? desc.id.c_str() : desc.name.c_str();

				// 同名ツールでもIDが衝突しないよう、登録IDでスコープを分ける。
				ImGui::PushID(desc.id.c_str());
				if (ImGui::MenuItem(label, nullptr, false, enabled)) {
					openToolID = desc.id;
				}
				DrawToolTooltip(desc, enabled);
				ImGui::PopID();
			}
			ImGui::SetWindowFontScale(1.0f);
			ImGui::EndMenu();
		}
		ImGui::PopID();
		first = last;
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();

	// 一覧の走査が終わってから開く。既存インスタンスを再利用する。
	if (!openToolID.empty()) {
		auto* tool = dynamic_cast<IEditorTool*>(
			ToolRegistry::GetInstance().Find(openToolID));
		if (tool && tool->IsEnabled(toolContext)) {
			tool->OpenEditorTool();
		}
	}
}

void Engine::EditorToolUI::DrawWindows(const EditorPanelContext& context) {
	if (!CanDrawToolUI(context)) {
		EndScenePreviews();
		return;
	}

	EditorToolContext editorToolContext{};
	editorToolContext.panelContext = &context;
	editorToolContext.toolContext = MakeToolContext(context);

	const auto tools = CollectEditorTools();
	for (IEditorTool* tool : tools) {
		// IsEnabledで一括スキップしない。
		// 開閉やPlay中の表示・編集制限は従来どおり各ツールが担当する。
		EditorToolFrameScope frameScope(*tool, editorToolContext);
		tool->DrawEditorTool(editorToolContext);
	}
}

bool Engine::EditorToolUI::HasPendingEdits() {

	const auto tools = CollectEditorTools();
	return std::ranges::any_of(tools, [](const IEditorTool* tool) {
		return tool->HasPendingEdits();
		});
}

void Engine::EditorToolUI::EndScenePreviews() {

	// Worldの値を戻してから保存と切替へ進む
	for (IEditorTool* tool : CollectEditorTools()) tool->EndScenePreview();
}

void Engine::EditorToolUI::RequestResolvePendingEdits() {

	const auto tools = CollectEditorTools();
	for (IEditorTool* tool : tools) {
		if (tool->HasPendingEdits()) {
			tool->RequestResolvePendingEdits();
		}
	}
}

Engine::EditorToolCloseResult Engine::EditorToolUI::ConsumePendingEditCloseResult() {

	bool accepted = false;
	const auto tools = CollectEditorTools();
	for (IEditorTool* tool : tools) {
		const EditorToolCloseResult result =
			tool->ConsumePendingEditCloseResult();
		if (result == EditorToolCloseResult::Cancelled) {
			return result;
		}
		accepted |= result == EditorToolCloseResult::Accepted;
	}
	return accepted ?
		EditorToolCloseResult::Accepted :
		EditorToolCloseResult::None;
}
