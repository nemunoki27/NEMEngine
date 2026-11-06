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

	void DrawLogEntry(const Engine::Logger::LogEntry& log, const ImVec2& size) {

		ImGui::PushID(static_cast<int>(log.sequence >> 32));
		ImGui::PushID(static_cast<int>(log.sequence));
		if (ImGui::IsRectVisible(size)) {
			ImGui::PushStyleColor(ImGuiCol_Text, ToLevelColor(log.level));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x);
			ImGui::TextUnformatted(log.message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();

			if (ImGui::IsItemClicked()) {
				ImGui::SetClipboardText(log.message.c_str());
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("クリックでコピー");
			}
		} else {
			ImGui::Dummy(size);
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PopID();
		ImGui::PopID();
	}

}

//============================================================================
//	ConsolePanel classMethods
//============================================================================
void Engine::ConsolePanel::RefreshLogs(float width) {

	const bool logsChanged = Logger::RefreshRecentLogs(logRevision_, logs_);
	if (logsChanged) {
		for (auto& log : logs_) {
			log.message = std::string(log.type == LogType::Engine ? "[Engine] [" : "[Game] [") +
				ToLevelLabel(log.level) + "] " + log.message;
		}
	}
	if (!logsChanged && logWidth_ == width && logFontSize_ == ImGui::GetFontSize() && logFont_ == ImGui::GetFont()) {
		return;
	}
	logWidth_ = width;
	logFontSize_ = ImGui::GetFontSize();
	logFont_ = ImGui::GetFont();
	logHeights_.resize(logs_.size());
	for (size_t index = 0; index < logs_.size(); ++index) {
		logHeights_[index] = ImGui::CalcTextSize(logs_[index].message.c_str(), nullptr, false, width).y;
	}
}

void Engine::ConsolePanel::DrawLogs() {

	const bool visible = ImGui::BeginChild("##LogList", ImVec2(0.0f, 0.0f), false);
	if (!visible) {
		ImGui::EndChild();
		return;
	}
	ImGui::SetWindowFontScale(0.74f);
	const float width = ImGui::GetContentRegionAvail().x;
	RefreshLogs(width);

	if (logs_.empty()) {
		ImGui::TextDisabled("ログはありません");
	}

	for (size_t index = logs_.size(); index > 0; --index) {
		DrawLogEntry(logs_[index - 1], ImVec2(width, logHeights_[index - 1]));
	}

	ImGui::SetWindowFontScale(1.0f);
	// ログ欄の右クリックから保存先を開く
	if (ImGui::BeginPopupContextWindow("LogContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		if (ImGui::MenuItem("ログをエクスプローラーで開く")) {
			EditorShell::OpenDirectory(Logger::GetLogDir());
		}
		ImGui::EndPopup();
	}

	ImGui::EndChild();
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

	DrawLogs();

	ImGui::End();
}
