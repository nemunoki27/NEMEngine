#include "EditorToolUI.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <algorithm>
#include <string_view>

//============================================================================
//	EditorToolUI classMethods
//============================================================================

namespace {

	// 空IDの位置には音声設定を表示する
	constexpr std::string_view kProjectSettingsTools[] = {
		"engine.tag_manager", "engine.collision_manager", "engine.script_execution_order",
		"engine.input_device", "", "engine.sceneViewCamera"
	};

	// メニューの所属だけを判定しツールの更新順序は変えない
	bool IsProjectSettingsTool(std::string_view id) {

		return std::ranges::any_of(kProjectSettingsTools, [id](std::string_view candidate) { return candidate == id; });
	}

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

	std::vector<std::shared_ptr<Engine::IEditorTool>> CollectEditorTools() {
		const auto registered = Engine::ToolRegistry::GetInstance().GetToolSnapshot();
		std::vector<std::shared_ptr<Engine::IEditorTool>> result;
		result.reserve(registered.size());

		// 登録順と描画中のツール寿命を保持する
		for (const auto& tool : registered) {
			if (auto editorTool = std::dynamic_pointer_cast<Engine::IEditorTool>(tool)) {
				result.push_back(std::move(editorTool));
			}
		}
		return result;
	}

	bool IsRegistered(const std::shared_ptr<Engine::IEditorTool>& tool) {
		return Engine::ToolRegistry::GetInstance().Find(tool->GetDescriptor().id) == tool.get();
	}

	const char* OwnerLabel(Engine::ToolOwner owner) {
		return owner == Engine::ToolOwner::Game ? "ゲーム" : "エンジン";
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

	// 登録と実行条件を確認してメニュー項目を表示する
	void DrawToolMenuItem(const std::shared_ptr<Engine::IEditorTool>& tool,
		const Engine::ToolContext& context, std::string& openToolID) {

		if (!tool || !IsRegistered(tool)) return;
		const auto& descriptor = tool->GetDescriptor();
		const bool enabled = tool->IsEnabled(context);
		const char* label = descriptor.name.empty() ? descriptor.id.c_str() : descriptor.name.c_str();
		ImGui::PushID(descriptor.id.c_str());
		if (ImGui::MenuItem(label, nullptr, false, enabled)) openToolID = descriptor.id;
		DrawToolTooltip(descriptor, enabled);
		ImGui::PopID();
	}

	// メニュー描画後に要求されたツールを開く
	void OpenRequestedTool(const std::string& id, const Engine::ToolContext& context) {

		if (id.empty()) return;
		const auto tool = std::dynamic_pointer_cast<Engine::IEditorTool>(Engine::ToolRegistry::GetInstance().Acquire(id));
		if (tool && IsRegistered(tool) && tool->IsEnabled(context)) tool->OpenEditorTool();
	}

	// 音声設定は既存の即時保存を維持する
	void DrawAudioSettingsMenu() {

		if (!ImGui::BeginMenu("音声設定")) return;
		Engine::Audio* audio = Engine::Audio::GetInstance();
		bool background = audio->IsPlayInBackgroundEnabled();
		if (ImGui::Checkbox("バックグラウンド再生", &background) && !audio->SetPlayInBackground(background)) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn, "音声設定を保存できません");
		}
		ImGui::EndMenu();
	}

	//============================================================================
	//	EditorToolFrameScope class
	//	描画終了時に借用した編集状態を解除する
	//============================================================================
	class EditorToolFrameScope final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Engine::IEditorTool& tool_;
	};
}

void Engine::EditorToolUI::DrawMenu(const EditorPanelContext& context) {
	if (!CanDrawToolUI(context) || !ImGui::BeginMenu("ツール")) {
		return;
	}

	ImGui::SetWindowFontScale(0.8f);

	auto tools = CollectEditorTools();
	std::erase_if(tools, [](const auto& tool) { return IsProjectSettingsTool(tool->GetDescriptor().id); });
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

		// 空カテゴリと同名カテゴリのIDを分ける
		ImGui::PushID(category.c_str());
		if (ImGui::BeginMenu(category.empty() ? "その他" : category.c_str())) {
			ImGui::SetWindowFontScale(0.8f);
			for (std::size_t index = first; index < last; ++index) {
				DrawToolMenuItem(tools[index], toolContext, openToolID);
			}
			ImGui::SetWindowFontScale(1.0f);
			ImGui::EndMenu();
		}
		ImGui::PopID();
		first = last;
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();

	OpenRequestedTool(openToolID, toolContext);
}

void Engine::EditorToolUI::DrawProjectSettingsMenu(const EditorPanelContext& context) {

	if (!CanDrawToolUI(context) || !ImGui::BeginMenu("プロジェクト設定")) return;
	ImGui::SetWindowFontScale(0.8f);
	const ToolContext toolContext = MakeToolContext(context);
	std::string openToolID;
	// プロジェクト設定は用途順に並べる
	for (const std::string_view id : kProjectSettingsTools) {
		if (id.empty()) {
			DrawAudioSettingsMenu();
			continue;
		}
		const auto tool = std::dynamic_pointer_cast<IEditorTool>(ToolRegistry::GetInstance().Acquire(id));
		DrawToolMenuItem(tool, toolContext, openToolID);
	}
	ImGui::SetWindowFontScale(1.0f);
	ImGui::EndMenu();
	OpenRequestedTool(openToolID, toolContext);
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
	for (const auto& tool : tools) {
		if (!IsRegistered(tool)) {
			continue;
		}
		// Play中の編集制限は各ツールで判定する
		EditorToolFrameScope frameScope(*tool, editorToolContext);
		if (IsRegistered(tool)) {
			tool->DrawEditorTool(editorToolContext);
		}
	}
}

bool Engine::EditorToolUI::HasPendingEdits() {

	const auto tools = CollectEditorTools();
	return std::ranges::any_of(tools, [](const auto& tool) {
		return IsRegistered(tool) && tool->HasPendingEdits();
		});
}

void Engine::EditorToolUI::EndScenePreviews() {

	// Worldの値を戻してから保存と切替へ進む
	for (const auto& tool : CollectEditorTools()) {
		if (IsRegistered(tool)) tool->EndScenePreview();
	}
}

void Engine::EditorToolUI::RequestResolvePendingEdits() {

	const auto tools = CollectEditorTools();
	for (const auto& tool : tools) {
		if (IsRegistered(tool) && tool->HasPendingEdits() && IsRegistered(tool)) {
			tool->RequestResolvePendingEdits();
		}
	}
}

Engine::EditorToolCloseResult Engine::EditorToolUI::ConsumePendingEditCloseResult() {

	bool accepted = false;
	const auto tools = CollectEditorTools();
	for (const auto& tool : tools) {
		if (!IsRegistered(tool)) {
			continue;
		}
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
