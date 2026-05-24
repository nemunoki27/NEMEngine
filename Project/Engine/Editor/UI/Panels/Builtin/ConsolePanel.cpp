#include "ConsolePanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>

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

	void DrawDescriptorUsage(const char* label, const Engine::BaseDescriptor& descriptor, float width) {

		ImGui::BeginChild(label, ImVec2(width, 0.0f), true);
		ImGui::Text("%s: %u / %u", label, descriptor.GetUseDescriptorCount(), descriptor.GetMaxDescriptorCount());
		ImGui::Separator();

		if (descriptor.GetUseDescriptorCount() == 0) {
			ImGui::TextDisabled("No descriptors.");
			ImGui::EndChild();
			return;
		}

		for (uint32_t index = 0; index < descriptor.GetHighWaterMark(); ++index) {
			if (!descriptor.IsAllocated(index)) {
				continue;
			}

			const std::string_view name = descriptor.GetResourceName(index);
			ImGui::Text("index%u: %s", index, name.empty() ? "Unknown" : name.data());
		}

		ImGui::EndChild();
	}

	void DrawGPUResourceTab(Engine::GraphicsCore* graphicsCore) {

		if (!graphicsCore) {
			ImGui::TextDisabled("GraphicsCore is not available.");
			return;
		}

		const ImVec2 region = ImGui::GetContentRegionAvail();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float childWidth = (region.x - spacing * 2.0f) / 3.0f;

		DrawDescriptorUsage("SRV", graphicsCore->GetSRVDescriptor(), childWidth);
		ImGui::SameLine();
		DrawDescriptorUsage("RTV", graphicsCore->GetRTVDescriptor(), childWidth);
		ImGui::SameLine();
		DrawDescriptorUsage("DSV", graphicsCore->GetDSVDescriptor(), childWidth);
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
			if (ImGui::BeginTabBar("ConsoleEngineTabBar")) {
				if (ImGui::BeginTabItem("Log")) {

					DrawLogTab(Engine::LogType::Engine, "##EngineLogList");
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("GPUResource")) {

					DrawGPUResourceTab(context.graphicsCore);

					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}