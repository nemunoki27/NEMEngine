#include "ConsolePanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Logger.h>

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

	void DrawLogEntry(int index, const std::string& text, spdlog::level::level_enum level) {

		ImGui::PushID(index);
		ImGui::PushStyleColor(ImGuiCol_Text, ToLevelColor(level));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		if (ImGui::IsItemClicked()) {
			ImGui::SetClipboardText(text.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Click to copy");
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PopID();
	}

	void DrawLogTab(Engine::LogType type, const char* childID) {

		const auto logs = Engine::Logger::GetRecentLogs(type);

		ImGui::TextDisabled("Showing latest %zu logs (max 30)", logs.size());

		ImGui::Separator();
		ImGui::BeginChild(childID, ImVec2(0.0f, 0.0f), false);
		ImGui::SetWindowFontScale(0.74f);

		if (logs.empty()) {
			ImGui::TextDisabled("No logs.");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::EndChild();
			return;
		}

		for (int i = static_cast<int>(logs.size()) - 1; i >= 0; --i) {

			const auto& log = logs[static_cast<std::size_t>(i)];
			const std::string text = std::string("[") + ToLevelLabel(log.level) + "] " + log.message;

			DrawLogEntry(i, text, log.level);
		}

		ImGui::SetWindowFontScale(1.0f);
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

	if (!ImGui::Begin("Console", &context.layoutState->showConsole)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("ConsoleTabBar")) {
		//============================================================================
		//	ゲームログ
		//============================================================================
		if (ImGui::BeginTabItem("Game")) {

			DrawLogTab(Engine::LogType::GameLogic, "##GameLogList");
			ImGui::EndTabItem();
		}
		//============================================================================
		//	エンジンログ
		//============================================================================
		if (ImGui::BeginTabItem("Engine")) {

			DrawLogTab(Engine::LogType::Engine, "##EngineLogList");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}
