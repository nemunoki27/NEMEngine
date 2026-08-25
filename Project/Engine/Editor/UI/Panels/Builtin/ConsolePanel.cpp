#include "ConsolePanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <algorithm>
#include <cstdio>
#include <vector>

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
			ImGui::SetTooltip("クリックでコピー");
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

namespace {

	constexpr float kGPUPassViewWidth = 360.0f;
	constexpr float kGPUPassTooltipMargin = 8.0f;
	constexpr size_t kGPUPassViewColumns = 2;

	// 交互に描画されるGameViewとSceneViewの最新計測結果を保持する
	struct GPUPassViewSnapshot {

		std::string name{};
		std::vector<Engine::FrameProfiler::GPUPassTime> passTimes{};
		float totalMilliseconds = 0.0f;
	};

	struct GPUPassTooltipState {

		std::vector<GPUPassViewSnapshot> views{};

		void Update(const Engine::FrameProfiler& profiler) {

			if (!profiler.HasGPUData()) {
				return;
			}

			std::vector<GPUPassViewSnapshot> updatedViews{};
			for (const Engine::FrameProfiler::GPUPassTime& pass : profiler.GetGPUPassTimes()) {

				const size_t slash = pass.name.find('/');
				const std::string viewName = slash != std::string::npos ?
					pass.name.substr(0, slash) : std::string();
				auto view = std::find_if(updatedViews.begin(), updatedViews.end(),
					[&viewName](const GPUPassViewSnapshot& snapshot) {
						return snapshot.name == viewName;
					});
				if (view == updatedViews.end()) {
					updatedViews.push_back({ .name = viewName });
					view = std::prev(updatedViews.end());
				}
				view->passTimes.push_back(pass);
				view->totalMilliseconds += pass.milliseconds;
			}

			for (GPUPassViewSnapshot& updated : updatedViews) {

				auto cached = std::find_if(views.begin(), views.end(),
					[&updated](const GPUPassViewSnapshot& snapshot) {
						return snapshot.name == updated.name;
					});
				if (cached != views.end()) {
					*cached = std::move(updated);
				} else {
					views.push_back(std::move(updated));
				}
			}

			// 2つの主要ビューは常に同じ順序で表示する
			const auto priority = [](const std::string& name) {
				if (name == "Game") {
					return 0;
				}
				if (name == "Scene") {
					return 1;
				}
				return 2;
			};
			std::stable_sort(views.begin(), views.end(),
				[&priority](const GPUPassViewSnapshot& lhs, const GPUPassViewSnapshot& rhs) {
					return priority(lhs.name) < priority(rhs.name);
				});
		}
	};

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

	void DrawGPUPassView(const GPUPassViewSnapshot& view) {

		ImGui::Text("[%s] GPU合計 : %.3f ms",
			view.name.empty() ? "-" : view.name.c_str(), view.totalMilliseconds);
		ImGui::Spacing();

		if (ImGui::BeginTable("##GPUPassTimes", 2,
			ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingFixedFit)) {

			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 230.0f);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableHeadersRow();

			for (const Engine::FrameProfiler::GPUPassTime& pass : view.passTimes) {

				const size_t slash = pass.name.find('/');
				const std::string label = slash != std::string::npos ?
					pass.name.substr(slash + 1) : pass.name;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(label.c_str());

				ImGui::TableSetColumnIndex(1);
				char timeText[32];
				std::snprintf(timeText, sizeof(timeText), "%.3f ms", pass.milliseconds);
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

	void DrawGPUPassTooltip(const GPUPassTooltipState& state) {

		const ImGuiViewport* viewport = ImGui::GetWindowViewport();
		const float tooltipWidth = GetGPUPassTooltipWidth(state.views.size(), *viewport);
		SetNextGPUPassTooltipWindow(tooltipWidth);
		ImGui::BeginTooltip();
		if (state.views.empty()) {

			ImGui::TextUnformatted("GPU計測データなし");
			ImGui::EndTooltip();
			return;
		}

		for (size_t firstView = 0; firstView < state.views.size();
			firstView += kGPUPassViewColumns) {

			const size_t columnCount = std::min(kGPUPassViewColumns,
				state.views.size() - firstView);
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
					DrawGPUPassView(state.views[firstView + column]);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
		}
		ImGui::EndTooltip();
	}

	// 計測タブ: FrameProfilerのフレーム計測結果を表示する
	void DrawMeasurementTab() {

		const Engine::FrameProfiler& profiler = Engine::FrameProfiler::GetInstance();

		ImGui::Text("FPS               : %.1f", profiler.GetFps());
		ImGui::Text("DeltaTime         : %.3f ms", profiler.GetDeltaTimeSec() * 1000.0f);
		ImGui::Text("起動からの経過時間  : %.2f s", profiler.GetTotalTimeSec());

		ImGui::Separator();

		ImGui::Text("更新全体          : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Update));
		ImGui::Text("ECSシステム       : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Ecs));
		if (ImGui::IsItemHovered()) {

			ImGui::BeginTooltip();
			if (profiler.HasEcsSystemData()) {

				ImGui::TextUnformatted("システム処理時間(処理順)");
				ImGui::Separator();
				for (const Engine::FrameProfiler::NamedTime& system : profiler.GetEcsSystemTimes()) {
					ImGui::Text("%-28s : %.3f ms", system.name.c_str(), system.milliseconds);
				}
			} else {

				ImGui::TextUnformatted("システム計測データなし");
			}
			ImGui::EndTooltip();
		}
		// ECSのarchetype数でForEachが走査する数、多いほどquery plan cacheの効果が見込める
		// コメントアウトして、必要な時にも表示してチェックする
		//ImGui::Text("Archetype数       : %u", profiler.GetArchetypeCount());
		const Engine::FrameProfiler::ECSStatistics& ecsStatistics = profiler.GetECSStatistics();
		ImGui::Text("Entity数          : %u", ecsStatistics.entityCount);
		/*ImGui::Text("Chunk             : %u / %u", ecsStatistics.allocatedChunkCount, ecsStatistics.chunkSlotCount);
		ImGui::Text("Chunkメモリ       : %.2f / %.2f MiB",
			static_cast<double>(ecsStatistics.payloadBytes) / (1024.0 * 1024.0),
			static_cast<double>(ecsStatistics.allocatedChunkBytes) / (1024.0 * 1024.0));*/
			/*	ImGui::Text("構造移動          : %llu (%llu Components / %.2f KiB)",
					static_cast<unsigned long long>(ecsStatistics.structuralMigrationCount),
					static_cast<unsigned long long>(ecsStatistics.relocatedComponentCount),
					static_cast<double>(ecsStatistics.relocatedComponentBytes) / 1024.0);*/
		ImGui::Text("C#処理            : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Script));

		// 描画処理でホバーでGPUの処理時間を各パスごとに表示する
		ImGui::Text("描画処理          : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Draw));
		static GPUPassTooltipState gpuPassTooltip{};
		gpuPassTooltip.Update(profiler);
		const bool drawTimeHovered = ImGui::IsItemHovered();
		if (drawTimeHovered) {
			DrawGPUPassTooltip(gpuPassTooltip);
		}

		// Meshバッチ構築/GPU転送のCPUコストで描画処理の内訳、staticキャッシュMISSやSkinned/Billboardで増える
		ImGui::Text("  Meshバッチ転送  : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::MeshBatchUpload));

		// GPU完了待ちでCPUがブロックした時間、大きいほどフレームコンテキスト多重化の効果が見込める
		ImGui::Text("GPU待ち           : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::GPUWait));

		//ImGui::Separator();

		/*ImGui::Text("Skinning          : %u Dispatch / %u Instances", rendering.skinningDispatchCount, rendering.skinnedInstanceCount);
		ImGui::Text("BLAS              : %u Build / %u Refit / %u Skip", rendering.blasBuildCount, rendering.blasRefitCount, rendering.blasSkipCount);
		ImGui::Text("BLAS Geometry     : %u", rendering.blasGeometryCount);
		ImGui::Text("TLAS Instance     : %u", rendering.tlasInstanceCount);
		ImGui::Text("TLAS              : %u Build / %u Refit / %u Skip",
			rendering.tlasBuildCount, rendering.tlasRefitCount, rendering.tlasSkipCount);
		ImGui::Text("Cluster           : %u / %u Lights / %u Indices",
			rendering.clusterCount, rendering.clusterLocalLightCount, rendering.clusterLightIndexCount);
		ImGui::Text("Cluster Overflow  : %u", rendering.clusterOverflowCount);*/
		/*ImGui::Text("Frame Context     : %u / %u  Queue=%u", rendering.frameContextIndex, rendering.frameContextCount,
			rendering.queuedFrameCount);*/
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
		//	エンジンログ
		//============================================================================
		if (ImGui::BeginTabItem("Engine")) {
			if (ImGui::BeginTabBar("ConsoleEngineTabBar")) {
				if (ImGui::BeginTabItem("Measurement")) {

					DrawMeasurementTab();
					ImGui::EndTabItem();
				}
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
		//============================================================================
		//	ゲームログ
		//============================================================================
		if (ImGui::BeginTabItem("Game")) {

			DrawLogTab(Engine::LogType::GameLogic, "##GameLogList");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}
