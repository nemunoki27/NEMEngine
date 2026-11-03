#include "ConsolePanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/Utility/EditorShell.h>

namespace {

	const char* ToLevelLabel(spdlog::level::level_enum level) {

		switch (level) {
		case spdlog::level::trace: return "Trace";
		case spdlog::level::debug: return "Debug";
		case spdlog::level::info: return "Info";
		case spdlog::level::warn: return "Warn";
		case spdlog::level::err: return "Error";
		case spdlog::level::critical: return "Critical";
		default: return "Log";
		}
	}

	ImVec4 ToLevelColor(spdlog::level::level_enum level) {

		switch (level) {
		case spdlog::level::warn:
			return ImVec4(0.95f, 0.80f, 0.25f, 1.0f);
		case spdlog::level::err:
		case spdlog::level::critical:
			return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
		default:
			return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}
	}

	void DrawLogEntry(uint64_t sequence, const std::string& text, spdlog::level::level_enum level) {

		const std::string id = std::to_string(sequence);
		ImGui::PushID(id.c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, ToLevelColor(level));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		if (ImGui::IsItemClicked()) {
			ImGui::SetClipboardText(text.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("クリックでコピー");
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PopID();
	}

	void DrawLogs(const char* childID) {

		const auto logs = Engine::Logger::GetRecentLogs();

		ImGui::BeginChild(childID, ImVec2(0.0f, 0.0f), false);
		ImGui::SetWindowFontScale(0.74f);

		if (logs.empty()) {
			ImGui::TextDisabled("ログはありません");
		}

		for (int i = static_cast<int>(logs.size()) - 1; i >= 0; --i) {

			const auto& log = logs[static_cast<std::size_t>(i)];
			const std::string text = std::string(log.type == Engine::LogType::Engine ? "[Engine] [" : "[Game] [") +
				ToLevelLabel(log.level) + "] " + log.message;

			DrawLogEntry(log.sequence, text, log.level);
		}

		ImGui::SetWindowFontScale(1.0f);
		// ログ欄の右クリックから保存先を開く
		if (ImGui::BeginPopupContextWindow("LogContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
			if (ImGui::MenuItem("ログをエクスプローラーで開く")) {
				Engine::EditorShell::OpenDirectory(Engine::Logger::GetLogDir());
			}
			ImGui::EndPopup();
		}

		ImGui::EndChild();
	}
}

//============================================================================
//	ConsolePanel classMethods
//============================================================================
void Engine::ConsolePanel::Draw(const EditorPanelContext& context) {

	// コンソールパネルの表示状態を確認
	if (!context.layoutState->showConsole) {
		return;
	}

	if (!Engine::EditorWindowPresentation::Begin(MakeWindowName("Console").c_str(), &context.layoutState->showConsole)) {
		ImGui::End();
		return;
	}

	DrawLogs("##LogList");

	ImGui::End();
}
