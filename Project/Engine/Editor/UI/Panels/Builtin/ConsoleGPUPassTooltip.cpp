#include "ConsoleGPUPassTooltip.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <algorithm>
#include <cstdio>
#include <vector>

// imgui
#include <imgui.h>

namespace {

	constexpr float kGPUPassViewWidth = 360.0f;
	constexpr float kGPUPassTooltipMargin = 8.0f;
	constexpr size_t kGPUPassViewColumns = 2;

	float GetGPUPassTooltipWidth(size_t viewCount, const ImGuiViewport& viewport) {

		const size_t columns = std::clamp(viewCount, size_t{ 1 }, kGPUPassViewColumns);
		const float desiredWidth = kGPUPassViewWidth * static_cast<float>(columns) +
			ImGui::GetStyle().ItemSpacing.x * static_cast<float>(columns - 1);
		return std::max(1.0f, std::min(desiredWidth,
			viewport.WorkSize.x - kGPUPassTooltipMargin * 2.0f));
	}

	void SetNextGPUPassTooltipWindow(float tooltipWidth) {

		const ImGuiViewport* viewport = ImGui::GetWindowViewport();
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const ImVec2 workMin = viewport->WorkPos;
		const ImVec2 workMax(
			viewport->WorkPos.x + viewport->WorkSize.x,
			viewport->WorkPos.y + viewport->WorkSize.y);

		float x = itemMax.x + kGPUPassTooltipMargin;
		if (workMax.x < x + tooltipWidth) {
			x = itemMin.x - kGPUPassTooltipMargin - tooltipWidth;
		}
		const float minX = workMin.x + kGPUPassTooltipMargin;
		const float maxX = std::max(minX,
			workMax.x - tooltipWidth - kGPUPassTooltipMargin);
		x = std::clamp(x, minX, maxX);

		const float maxHeight = std::max(1.0f,
			viewport->WorkSize.y - kGPUPassTooltipMargin * 2.0f);
		ImGui::SetNextWindowPos(
			ImVec2(x, workMin.y + kGPUPassTooltipMargin), ImGuiCond_Always);
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(tooltipWidth, 0.0f), ImVec2(tooltipWidth, maxHeight));
	}

	void DrawGPUPassView(const Engine::ConsoleGPUPassViewSnapshot& view) {

		ImGui::Text("[%s] GPU合計 : %.3f ms",
			view.name.empty() ? "-" : view.name.c_str(), view.totalMilliseconds);
		ImGui::Spacing();

		if (ImGui::BeginTable("##GPUPassTimes", 2,
			ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingFixedFit)) {

			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 230.0f);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableHeadersRow();

			for (const auto& cached : view.passTimes) {

				const auto& pass = cached.time;

				const size_t slash = pass.name.find('/');
				const std::string label = slash != std::string::npos ?
					pass.name.substr(slash + 1) : pass.name;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(label.c_str());

				ImGui::TableSetColumnIndex(1);
				char timeText[32];
				if (pass.frameID != 0) std::snprintf(timeText, sizeof(timeText), "%.3f ms", pass.milliseconds);
				else std::snprintf(timeText, sizeof(timeText), "--");
				const float cellWidth = ImGui::GetContentRegionAvail().x;
				const float textWidth = ImGui::CalcTextSize(timeText).x;
				if (textWidth < cellWidth) {
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cellWidth - textWidth);
				}
				ImGui::TextUnformatted(timeText);
			}
			ImGui::EndTable();
		}
	}

	void DrawGPUPassTooltip(const Engine::ConsoleGPUPassTooltipState& state) {

		const ImGuiViewport* viewport = ImGui::GetWindowViewport();
		const float tooltipWidth = GetGPUPassTooltipWidth(state.GetViews().size(), *viewport);
		SetNextGPUPassTooltipWindow(tooltipWidth);
		ImGui::BeginTooltip();
		if (state.GetViews().empty()) {

			ImGui::TextUnformatted("GPU計測データなし");
			ImGui::EndTooltip();
			return;
		}

		for (size_t firstView = 0; firstView < state.GetViews().size();
			firstView += kGPUPassViewColumns) {

			const size_t columnCount = std::min(kGPUPassViewColumns,
				state.GetViews().size() - firstView);
			ImGui::PushID(static_cast<int>(firstView));
			if (ImGui::BeginTable("##GPUPassViews", static_cast<int>(columnCount),
				ImGuiTableFlags_SizingStretchSame)) {

				for (size_t column = 0; column < columnCount; ++column) {
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
				}
				ImGui::TableNextRow();
				for (size_t column = 0; column < columnCount; ++column) {

					ImGui::TableSetColumnIndex(static_cast<int>(column));
					ImGui::PushID(static_cast<int>(column));
					DrawGPUPassView(state.GetViews()[firstView + column]);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
		}
		ImGui::EndTooltip();
	}

}

void Engine::DrawConsoleGPUPassTooltip(const FrameProfiler& profiler, bool hovered) {

	static ConsoleGPUPassTooltipState state;
	// ホバーしていない間も未計測の時間を進める
	state.Update(profiler.GetGPUPassTimes(), profiler.GetGPUFrameID(), profiler.HasGPUData(), ImGui::GetTime());
	if (hovered) DrawGPUPassTooltip(state);
}

void Engine::ConsoleGPUPassTooltipState::Update(std::span<const FrameProfiler::GPUPassTime> times, uint64_t frameID, bool hasData, double now) {

	// 同じGPU結果を繰り返し受けても有効時間を延ばさない
	if (!hasData || lastFrameID_ != frameID) {

		lastFrameID_ = frameID;
		for (auto& view : views_) {
			view.totalMilliseconds = 0.0f;
			for (auto& pass : view.passTimes) {
				if (pass.missingSince < 0.0) pass.missingSince = now;
				pass.time.frameID = 0;
			}
		}
		for (const auto& time : hasData ? times : std::span<const FrameProfiler::GPUPassTime>{}) {

			const size_t slash = time.name.find('/');
			const std::string name = slash != std::string::npos ? time.name.substr(0, slash) : std::string();
			auto view = std::find_if(views_.begin(), views_.end(), [&](const auto& item) { return item.name == name; });
			if (view == views_.end()) {
				views_.push_back({ .name = name });
				view = std::prev(views_.end());
			}
			auto pass = std::find_if(view->passTimes.begin(), view->passTimes.end(), [&](const auto& item) {
				return item.time.name == time.name && item.time.viewID == time.viewID;
			});
			if (pass == view->passTimes.end()) view->passTimes.push_back({ time, -1.0 });
			else *pass = { time, -1.0 };
			view->totalMilliseconds += time.milliseconds;
		}
	}
	// 三秒間更新されない行と空のグループを取り除く
	for (auto& view : views_) {
		std::erase_if(view.passTimes, [&](const auto& pass) { return pass.missingSince >= 0.0 && now - pass.missingSince >= 3.0; });
	}
	std::erase_if(views_, [](const auto& view) { return view.passTimes.empty(); });
	const auto priority = [](const std::string& name) { return name == "Game" ? 0 : name == "Scene" ? 1 : 2; };
	std::stable_sort(views_.begin(), views_.end(), [&](const auto& lhs, const auto& rhs) {
		return priority(lhs.name) < priority(rhs.name);
	});
}
