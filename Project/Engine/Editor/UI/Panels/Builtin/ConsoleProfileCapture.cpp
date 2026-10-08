#include "ConsolePanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Time/ProfileCapture.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// imgui
#include <imgui.h>

namespace {

	// 記録の終了理由を操作画面へ表示する
	const char* ProfileStopLabel(std::string_view reason) {

		if (reason == "frame_limit") { return "指定フレーム数に達しました"; }
		if (reason == "capacity_exceeded") { return "記録容量の上限に達しました"; }
		if (reason == "profiling_disabled") { return "計測が無効になりました"; }
		if (reason == "application_exit") { return "終了時に記録を停止しました"; }
		return "記録を終了しました";
	}

	// CPUの内訳は重複する計測区間を合算せず比較する
	std::pair<double, size_t> CalculateCPUAverage(const nlohmann::json& capture, size_t category) {

		double sum = 0.0;
		size_t count = 0;
		if (!capture.is_object() || !capture.contains("frames")) { return { sum, count }; }
		for (const auto& frame : capture["frames"]) {
			if (!frame.contains("cpuMilliseconds") || frame["cpuMilliseconds"].size() <= category) { continue; }
			sum += frame["cpuMilliseconds"][category].get<double>();
			++count;
		}
		return { count ? sum / static_cast<double>(count) : 0.0, count };
	}

	// Scriptの詳細区間を二重に加算しない
	std::pair<double, size_t> CalculateScriptAverage(const nlohmann::json& capture) {

		double sum = 0.0;
		size_t count = 0;
		if (!capture.is_object() || !capture.contains("frames")) { return { sum, count }; }
		for (const auto& frame : capture["frames"]) {
			if (!frame.contains("script") || frame["script"].value("status", std::string{}) != "complete") { continue; }
			for (const auto& row : frame["script"]["rows"]) {
				if (!row.value("detail", false)) { sum += row["selfMilliseconds"].get<double>(); }
			}
			++count;
		}
		return { count ? sum / static_cast<double>(count) : 0.0, count };
	}

	// 確定したGPU結果だけの平均を求める
	std::pair<double, size_t> CalculateGPUAverage(const nlohmann::json& capture) {

		double sum = 0.0;
		size_t count = 0;
		if (!capture.is_object() || !capture.contains("frames")) { return { sum, count }; }
		for (const auto& frame : capture["frames"]) {
			if (frame.value("gpuStatus", std::string{}) != "complete") { continue; }
			for (const auto& pass : frame["gpuPasses"]) {
				if (pass.contains("milliseconds") && pass["milliseconds"].is_number()) {
					sum += pass["milliseconds"].get<double>();
				}
			}
			++count;
		}
		return { count ? sum / static_cast<double>(count) : 0.0, count };
	}
}

void Engine::ConsolePanel::DrawCaptureControls() {

	auto& profiler = FrameProfiler::GetInstance();
	auto& capture = profiler.GetCapture();
	ImGui::SeparatorText("計測記録");
	ImGui::Text("CPU frame: %llu / GPU frame: %llu / %s", profiler.GetFrameID(),
		profiler.GetGPUFrameID(), profiler.GetGPUStatus().c_str());
	if (profiler.GetGPUFrameID() + 8 < profiler.GetFrameID()) {
		ImGui::TextUnformatted("GPU結果が更新されていません");
	}
	ImGui::BeginDisabled(capture.IsRecording());
	ImGui::InputInt("記録フレーム数", &captureFrameLimit_);
	if (ImGui::Button("記録開始")) {
		captureStatus_ = captureFrameLimit_ > 0 && profiler.StartCapture(static_cast<uint32_t>(captureFrameLimit_)) ?
			"" : "計測を有効にし、フレーム数を1から36000で指定してください";
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!capture.IsRecording());
	if (ImGui::Button("記録終了")) { capture.Stop(); captureStatus_ = "記録を終了しました"; }
	ImGui::EndDisabled();
	// 自動終了しても古い記録中の表示を残さない
	const auto& snapshot = capture.GetSnapshot();
	if (snapshot.is_object()) {
		ImGui::Text("%s / %zu frame", capture.IsRecording() ? "記録中" :
			ProfileStopLabel(snapshot.value("stopReason", std::string{})), snapshot["frames"].size());
	}
	ImGui::InputText("記録ファイル", capturePath_.data(), capturePath_.size());
	const auto path = RuntimePaths::GetSavedPath("Profiler") / Algorithm::PathFromUTF8(capturePath_.data());
	ImGui::BeginDisabled(capture.IsRecording());
	if (ImGui::Button("保存")) {
		captureStatus_ = capture.Save(path) ? "計測記録を保存しました" : "計測記録の保存に失敗しました";
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("比較用に読み込み")) {
		captureStatus_ = ProfileCapture::Load(path, comparisonCapture_) ?
			"比較用の記録を読み込みました" : "計測記録の読み込みに失敗しました";
	}
	if (comparisonCapture_.is_object() && capture.GetSnapshot().is_object()) {
		// 条件が違う計測を性能改善として比較しない
		const bool sameConditions = ProfileCapture::HasSameConditions(capture.GetSnapshot(), comparisonCapture_);
		ImGui::TextUnformatted(sameConditions ? "描画条件は一致しています" : "描画条件が異なるため参考値です");
		const auto [currentAverage, currentCount] = CalculateGPUAverage(capture.GetSnapshot());
		const auto [previousAverage, previousCount] = CalculateGPUAverage(comparisonCapture_);
		ImGui::Text("GPU平均: 現在 %.3f ms / 比較 %.3f ms", currentAverage, previousAverage);
		ImGui::Text("GPU確定フレーム: 現在 %zu / 比較 %zu", currentCount, previousCount);
		static constexpr const char* categories[] = {
			"Update", "ECS", "Script", "Draw", "GPU wait", "Mesh upload", "Mesh build", "Buffer transfer", "Material build"
		};
		if (ImGui::BeginTable("CaptureCPUComparison", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableSetupColumn("CPU区間");
			ImGui::TableSetupColumn("現在 (ms)");
			ImGui::TableSetupColumn("比較 (ms)");
			ImGui::TableHeadersRow();
			for (size_t index = 0; index < std::size(categories); ++index) {
				const auto [current, currentFrames] = CalculateCPUAverage(capture.GetSnapshot(), index);
				const auto [previous, previousFrames] = CalculateCPUAverage(comparisonCapture_, index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(categories[index]);
				ImGui::TableNextColumn();
				if (currentFrames) { ImGui::Text("%.3f", current); } else { ImGui::TextUnformatted("未計測"); }
				ImGui::TableNextColumn();
				if (previousFrames) { ImGui::Text("%.3f", previous); } else { ImGui::TextUnformatted("未計測"); }
			}
			ImGui::EndTable();
		}
		const auto [currentScript, currentScriptFrames] = CalculateScriptAverage(capture.GetSnapshot());
		const auto [previousScript, previousScriptFrames] = CalculateScriptAverage(comparisonCapture_);
		ImGui::Text("Script callback平均: 現在 %.3f ms (%zu) / 比較 %.3f ms (%zu)",
			currentScript, currentScriptFrames, previousScript, previousScriptFrames);
	}
	if (!captureStatus_.empty()) { ImGui::TextWrapped("%s", captureStatus_.c_str()); }
	ImGui::Separator();
}
