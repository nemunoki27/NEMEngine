#include "ConsolePanel.h"
#include "ConsoleGPUPassTooltip.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
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

	// 計測タブ: FrameProfilerのフレーム計測結果を表示する
	void DrawMeasurementTab() {

		const Engine::FrameProfiler& profiler = Engine::FrameProfiler::GetInstance();

		ImGui::Text("FPS               : %.1f", profiler.GetFPS());
		ImGui::Text("DeltaTime         : %.3f ms", profiler.GetDeltaTimeSec() * 1000.0f);
		ImGui::Text("起動からの経過時間  : %.2f s", profiler.GetTotalTimeSec());

		ImGui::Separator();

		ImGui::Text("更新全体          : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Update));
		ImGui::Text("ECSシステム       : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::ECS));
		if (ImGui::IsItemHovered()) {

			ImGui::BeginTooltip();
			if (profiler.HasECSSystemData()) {

				ImGui::TextUnformatted("システム処理時間(処理順)");
				ImGui::Separator();
				for (const Engine::FrameProfiler::NamedTime& system : profiler.GetECSSystemTimes()) {
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
		Engine::DrawConsoleGPUPassTooltip(profiler, ImGui::IsItemHovered());

		// Mesh更新のCPU時間を表示し、内訳と更新量はツールチップで確認する
		ImGui::Text("  Meshバッチ構築・転送 : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::MeshBatchUpload));
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			const auto& meshStats = profiler.GetRenderingStatistics();
			ImGui::Text("CPUバッチ構築 : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::MeshBatchBuild));
			ImGui::Text("バッファ転送CPU : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::MeshBufferTransfer));
			ImGui::Text("パラメータ構築 : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::MeshMaterialBuild));
			ImGui::Text("完全再構築 %u / Transform更新 %u / パラメータ更新 %u / 再利用 %u",
				meshStats.meshRebuildCount, meshStats.meshTransformUpdateCount,
				meshStats.meshParameterUpdateCount, meshStats.meshReuseCount);
			ImGui::Text("更新対象 %llu instances / 転送 %.2f KiB",
				static_cast<unsigned long long>(meshStats.meshUpdatedInstances), meshStats.meshTransferBytes / 1024.0);
			ImGui::TextUnformatted("時間は平均CPU時間、件数は直前フレームの全View・Pass合計です");
			ImGui::TextUnformatted("再利用は構成の再利用を表し、Transform・パラメータ更新を含みます");
			ImGui::TextUnformatted("親区間と内訳は加算しません。GPU実行時間ではありません");
			ImGui::EndTooltip();
		}

		// GPU完了待ちでCPUがブロックした時間、大きいほどフレームコンテキスト多重化の効果が見込める
		ImGui::Text("GPU待ち           : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::GPUWait));

		ImGui::Separator();

		const InputType inputType = Engine::Input::GetInstance()->GetType();
		ImGui::Text("入力デバイスタイプ: %s",
			inputType == InputType::GamePad ? "ゲームパッド" : "キーボード");

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
