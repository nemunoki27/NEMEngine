#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfileHistory.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfileSummary.h>
#include <Engine/Editor/UI/Panels/Builtin/ConsoleGPUPassTooltip.h>

// c++
#include <array>
#include <cmath>

namespace {

	// 表示行と合計が同じ有効な計測値を表しているか検証する
	bool HasConsistentGPUPassTotals(const Engine::ConsoleGPUPassTooltipState& state) {

		for (const auto& view : state.GetViews()) {
			float total = 0.0f;
			for (const auto& pass : view.passTimes) {
				if (pass.time.frameID != 0) total += pass.time.milliseconds;
			}
			if (std::abs(total - view.totalMilliseconds) > 0.0001f) return false;
		}
		return true;
	}

	// 待機の内訳とフレーム確定時の平均化を検証する
	bool TestDrawTiming() {

		using Category = Engine::FrameProfiler::Category;
		auto& profiler = Engine::FrameProfiler::GetInstance();
		const bool wasEnabled = profiler.IsEnabled();
		profiler.SetEnabled(false);
		profiler.SetEnabled(true);
		profiler.BeginFrame(0.0f, 0.0f);

		profiler.AddSample(Category::Draw, 15.0f);
		profiler.AddSample(Category::GPUWait, 2.0f);
		profiler.AddSample(Category::PresentCPU, 3.0f);
		profiler.AddSample(Category::FPSWait, 4.0f);
		profiler.BeginFrame(0.0f, 0.0f);
		bool result = profiler.GetLatestMs(Category::DrawCPU) == 6.0f &&
			profiler.GetLatestMs(Category::Draw) == 15.0f && profiler.GetLatestMs(Category::GPUWait) == 2.0f &&
			profiler.GetLatestMs(Category::PresentCPU) == 3.0f && profiler.GetLatestMs(Category::FPSWait) == 4.0f;

		// 今フレームへの加算で確定済みの表示値を変えない
		profiler.AddSample(Category::Draw, 10.0f);
		profiler.AddSample(Category::GPUWait, 0.5f);
		profiler.AddSample(Category::PresentCPU, 1.0f);
		result &= profiler.GetLatestMs(Category::DrawCPU) == 6.0f;
		profiler.BeginFrame(0.0f, 0.0f);
		result &= profiler.GetLatestMs(Category::DrawCPU) == 8.5f &&
			profiler.GetAverageMs(Category::DrawCPU) == 7.25f && profiler.GetLatestMs(Category::FPSWait) == 0.0f;

		// 丸めで内訳が全体を超えても負値を公開しない
		profiler.AddSample(Category::Draw, 1.0f);
		profiler.AddSample(Category::GPUWait, 2.0f);
		profiler.BeginFrame(0.0f, 0.0f);
		result &= profiler.GetLatestMs(Category::DrawCPU) == 0.0f &&
			std::abs(profiler.GetAverageMs(Category::DrawCPU) - 14.5f / 3.0f) < 0.0001f;
		profiler.BeginFrame(0.0f, 0.0f);
		result &= profiler.GetLatestMs(Category::DrawCPU) == 0.0f && profiler.GetLatestMs(Category::GPUWait) == 0.0f;

		// 計測の停止と再開で途中の累積を持ち越さない
		profiler.AddSample(Category::Draw, 100.0f);
		profiler.SetEnabled(false);
		profiler.AddSample(Category::Draw, 50.0f);
		profiler.AddSample(Category::FPSWait, 40.0f);
		profiler.BeginFrame(0.0f, 0.0f);
		profiler.SetEnabled(true);
		profiler.BeginFrame(0.0f, 0.0f);
		profiler.AddSample(Category::Draw, 5.0f);
		profiler.BeginFrame(0.0f, 0.0f);
		result &= profiler.GetLatestMs(Category::DrawCPU) == 5.0f &&
			profiler.GetAverageMs(Category::DrawCPU) == 5.0f && profiler.GetLatestMs(Category::FPSWait) == 0.0f;
		profiler.SetEnabled(false);
		profiler.SetEnabled(wasEnabled);
		return result;
	}
}

bool NEMTests::TestProfilerContracts() {

	Engine::FrameProfileHistory history;
	if (history.GetLatest() != 0.0f) return false;
	history.BeginFrame(true);
	history.Add(2.0f);
	history.Add(3.0f);
	if (history.GetAverage() != 5.0f || history.GetLatest() != 0.0f || history.GetCurrent() != 5.0f) return false;
	history.BeginFrame(false);
	history.Add(100.0f);
	if (history.GetAverage() != 5.0f || history.GetLatest() != 5.0f || history.GetCurrent() != 100.0f) return false;
	history.BeginFrame(false);
	if (history.GetAverage() != 52.5f || history.GetLatest() != 100.0f) return false;
	for (int i = 0; i < 8; ++i) {
		history.Add(4.0f);
		history.BeginFrame(false);
	}
	if (history.GetAverage() != 4.0f || history.GetLatest() != 4.0f) return false;
	// 計測の再開時に平均履歴と途中の累積をまとめて破棄する
	history.Add(50.0f);
	history.BeginFrame(true);
	if (history.GetAverage() != 0.0f || history.GetLatest() != 0.0f || history.GetCurrent() != 0.0f) return false;
	history.Add(7.0f);
	history.BeginFrame(false);
	if (history.GetAverage() != 7.0f || history.GetLatest() != 7.0f) return false;
	if (!TestDrawTiming()) return false;

	std::vector<Engine::ScriptProfileRow> rows(2);
	rows[0].name = "first";
	rows[0].history[0] = { 4.0, 1.0, 2 };
	rows[0].history[1] = { 2.0, 0.5, 1 };
	rows[1].name = "second";
	rows[1].history[1] = { 5.0, 3.0, 1 };
	const auto summary = Engine::SummarizeScriptProfile(rows, 1, 2, 1);
	return summary.size() == 2 && summary[0].name == "first" && summary[0].average == 3.0 &&
		summary[0].latest == 2.0 && summary[0].self == 0.5 && summary[0].averageCalls == 1.5 &&
		Engine::SummarizeScriptProfile(rows, 1, 2, 0)[0].name == "second";
}

bool NEMTests::TestConsoleGPUPassContracts() {

	// 未計測の連続時間で非表示にし再計測で復帰する
	Engine::ConsoleGPUPassTooltipState tooltip;
	const std::array<Engine::FrameProfiler::GPUPassTime, 1> measured{{{ "Game/Lighting", 1.0f, 1, "Game" }}};
	tooltip.Update(measured, 1, true, 0.0);
	tooltip.Update({}, 2, true, 0.1);
	tooltip.Update({}, 2, true, 3.0);
	if (tooltip.GetViews().size() != 1 || tooltip.GetViews()[0].passTimes[0].time.frameID != 0 ||
		tooltip.GetViews()[0].totalMilliseconds != 0.0f) return false;
	tooltip.Update({}, 2, true, 3.1001);
	if (!tooltip.GetViews().empty()) return false;
	tooltip.Update(measured, 3, true, 3.2);
	if (tooltip.GetViews().size() != 1) return false;
	tooltip.Update({}, 4, true, 3.3);
	tooltip.Update(measured, 5, true, 5.0);
	tooltip.Update({}, 6, true, 5.1);
	tooltip.Update({}, 6, true, 7.0);
	if (tooltip.GetViews().size() != 1) return false;
	tooltip.Update(measured, 7, true, 8.0);
	tooltip.Update(measured, 7, false, 8.1);
	tooltip.Update(measured, 7, false, 11.2);
	if (tooltip.GetViews().empty() || !HasConsistentGPUPassTotals(tooltip)) return false;

	// 計測待ちは行を消さず、ホバー中は欠測が続いても配置を変えない
	tooltip.Update(measured, 8, true, 11.3, true);
	tooltip.Update({}, 9, true, 11.4, true);
	tooltip.Update({}, 9, true, 15.0, true);
	if (tooltip.GetViews().size() != 1 || tooltip.GetViews()[0].passTimes[0].time.frameID != 0 ||
		!HasConsistentGPUPassTotals(tooltip)) return false;
	tooltip.Update({}, 9, true, 15.1, false);
	if (!tooltip.GetViews().empty()) return false;

	// ホバー中も新しいパスを追加し、既存行の順番を保つ
	Engine::ConsoleGPUPassTooltipState stable;
	const std::array<Engine::FrameProfiler::GPUPassTime, 2> initial{{
		{"Game/Opaque", 1.0f, 1, "CameraA"}, {"Game/Lighting", 2.0f, 1, "CameraA"}}};
	const std::array<Engine::FrameProfiler::GPUPassTime, 3> reordered{{
		{"Game/Lighting", 3.0f, 2, "CameraA"}, {"Game/AO", 4.0f, 2, "CameraA"}, {"Game/Opaque", 5.0f, 2, "CameraA"}}};
	stable.Update({}, 0, false, 0.0, true);
	stable.Update(initial, 1, true, 0.05, true);
	stable.Update(reordered, 2, true, 0.1, true);
	if (stable.GetViews().size() != 1 || stable.GetViews()[0].passTimes.size() != 3 ||
		stable.GetViews()[0].passTimes[0].time.name != "Game/Opaque" ||
		stable.GetViews()[0].passTimes[0].time.milliseconds != 5.0f ||
		stable.GetViews()[0].passTimes[2].time.name != "Game/AO" ||
		stable.GetViews()[0].totalMilliseconds != 12.0f || !HasConsistentGPUPassTotals(stable)) return false;
	stable.Update(reordered, 2, true, 0.15, true);
	if (stable.GetViews()[0].totalMilliseconds != 12.0f) return false;
	stable.Update(reordered, 3, true, 0.2, false);
	if (stable.GetViews()[0].passTimes.size() != 3 || stable.GetViews()[0].totalMilliseconds != 12.0f) return false;

	// 副Viewを間引いたフレームでは直近の計測値を維持する
	Engine::ConsoleGPUPassTooltipState alternating;
	const std::array<Engine::FrameProfiler::GPUPassTime, 1> game{{{"Game/Opaque", 1.5f, 1, "GameCamera"}}};
	const std::array<Engine::FrameProfiler::GPUPassTime, 1> gameAgain{{{"Game/Opaque", 1.7f, 3, "GameCamera"}}};
	const std::array<Engine::FrameProfiler::GPUPassTime, 1> scene{{{"Scene/Opaque", 2.0f, 2, "SceneCamera"}}};
	alternating.Update(game, 1, true, 0.0);
	alternating.Update(scene, 2, true, 0.1);
	if (alternating.GetViews().size() != 2 || alternating.GetViews()[0].passTimes[0].time.frameID != 1 ||
		alternating.GetViews()[0].totalMilliseconds != 1.5f || !HasConsistentGPUPassTotals(alternating)) return false;
	alternating.Update(gameAgain, 3, true, 0.2);
	alternating.Update(scene, 4, true, 0.3);
	if (alternating.GetViews()[0].passTimes[0].time.frameID != 3 || !HasConsistentGPUPassTotals(alternating)) return false;
	alternating.Update(scene, 5, true, 3.4);
	if (alternating.GetViews().size() != 1 || alternating.GetViews()[0].name != "Scene") return false;

	// 同名パスは同じView内だけで合算し、ホバー中の新しいViewも表示する
	Engine::ConsoleGPUPassTooltipState repeated;
	const std::array<Engine::FrameProfiler::GPUPassTime, 3> duplicates{{
		{"Game/Bloom", 1.0f, 1, "CameraA"}, {"Game/Bloom", 2.0f, 1, "CameraA"},
		{"Game/Lighting", 4.0f, 1, "CameraA"}}};
	repeated.Update(duplicates, 1, true, 0.0, true);
	if (repeated.GetViews().size() != 1 || repeated.GetViews()[0].passTimes.size() != 2 ||
		repeated.GetViews()[0].passTimes[0].time.milliseconds != 3.0f ||
		repeated.GetViews()[0].totalMilliseconds != 7.0f || !HasConsistentGPUPassTotals(repeated)) return false;
	const std::array<Engine::FrameProfiler::GPUPassTime, 4> cameras{{
		{"Scene/Bloom", 8.0f, 2, "SceneCamera"}, {"Game/Bloom", 16.0f, 2, "CameraB"},
		{"Game/Bloom", 2.0f, 2, "CameraA"}, {"Game/Bloom", 4.0f, 2, "CameraA"}}};
	repeated.Update(cameras, 2, true, 0.1, true);
	if (repeated.GetViews().size() != 3 || repeated.GetViews()[0].viewID != "CameraA" ||
		repeated.GetViews()[1].viewID != "SceneCamera" || repeated.GetViews()[2].viewID != "CameraB" ||
		repeated.GetViews()[0].totalMilliseconds != 6.0f || repeated.GetViews()[1].totalMilliseconds != 8.0f ||
		repeated.GetViews()[2].totalMilliseconds != 16.0f ||
		repeated.GetViews()[0].passTimes[1].time.frameID != 0 || !HasConsistentGPUPassTotals(repeated)) return false;
	repeated.Update(cameras, 2, true, 0.2, true);
	if (repeated.GetViews()[0].totalMilliseconds != 6.0f) return false;
	repeated.Update(cameras, 2, false, 0.3, true);
	if (repeated.HasCurrentData() || !HasConsistentGPUPassTotals(repeated)) return false;
	repeated.Update(cameras, 2, true, 0.4, false);
	if (repeated.GetViews()[1].viewID != "CameraB" || !HasConsistentGPUPassTotals(repeated)) return false;
	repeated.Update({}, 3, true, 0.5, true);
	repeated.Update({}, 3, true, 4.0, true);
	if (repeated.GetViews().size() != 3 || !HasConsistentGPUPassTotals(repeated)) return false;
	repeated.Update({}, 3, true, 4.1, false);
	return repeated.GetViews().empty();
}
