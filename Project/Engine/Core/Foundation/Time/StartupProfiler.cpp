#include "StartupProfiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
// windows
#include <Windows.h>

namespace {

	using Clock = std::chrono::steady_clock;
	struct StartupSample {
		uint32_t count = 0;
		double milliseconds = 0.0;
	};

	std::atomic<bool> active = false;
	std::mutex samplesMutex;
	std::array<StartupSample, static_cast<size_t>(Engine::StartupProfiler::Stage::Count)> samples{};
	Clock::time_point start;
	double launchOffset = 0.0;
	double uiTime = -1.0, gameTime = -1.0, sceneTime = -1.0;
	bool submitted = false, uiSubmitted = false;
	bool visibleGame = false, visibleScene = false, readyGame = false, readyScene = false;
	std::optional<std::array<bool, 2>> previousVisibleViews;
}

void Engine::StartupProfiler::Begin() {

	// WinMainまでの経過もプロセス起動時間へ含める
	FILETIME created{}, exited{}, kernel{}, user{}, now{};
	GetSystemTimePreciseAsFileTime(&now);
	start = Clock::now();
	if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
		const auto ticks = [](const FILETIME& value) {
			return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
		};
		if (ticks(created) <= ticks(now)) launchOffset = (ticks(now) - ticks(created)) / 10000.0;
	}
	active.store(true, std::memory_order_relaxed);
}

bool Engine::StartupProfiler::IsActive() {

	return active.load(std::memory_order_relaxed);
}

void Engine::StartupProfiler::AddSample(Stage stage, Clock::duration elapsed) {

	std::lock_guard lock(samplesMutex);
	auto& sample = samples[static_cast<size_t>(stage)];
	++sample.count;
	sample.milliseconds += std::chrono::duration<double, std::milli>(elapsed).count();
}

void Engine::StartupProfiler::SetFrame(bool ui, bool gameVisible, bool sceneVisible, bool gameReady, bool sceneReady) {

	submitted = true;
	uiSubmitted = ui;
	visibleGame = gameVisible;
	visibleScene = sceneVisible;
	readyGame = gameReady;
	readyScene = sceneReady;
}

void Engine::StartupProfiler::OnPresented() {

	if (!IsActive() || !submitted) return;
	submitted = false;
	const double elapsed = launchOffset + std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	if (uiSubmitted && uiTime < 0.0) uiTime = elapsed;
	if (visibleGame && readyGame && gameTime < 0.0) gameTime = elapsed;
	if (visibleScene && readyScene && sceneTime < 0.0) sceneTime = elapsed;
	// 初回DockingでPanelの表示状態が決まるまで、空のフレームを起動完了にしない
	const std::array visibleViews{visibleGame, visibleScene};
	const bool layoutStable = previousVisibleViews && *previousVisibleViews == visibleViews;
	previousVisibleViews = visibleViews;
	if (!layoutStable || (visibleGame && !readyGame) || (visibleScene && !readyScene)) return;

	active.store(false, std::memory_order_relaxed);
	std::lock_guard lock(samplesMutex);
	Logger::Output(LogType::Engine,
		"[EditorStartup] Present完了(ms) UI={:.1f} GameView={:.1f} SceneView={:.1f} 表示準備完了={:.1f} (-1は非表示)",
		uiTime, gameTime, sceneTime, elapsed);
	constexpr std::array names{"DXC", "GraphicsPSO", "ComputePSO", "RootSignature",
		"DXRLibrary", "DXRRootSignature", "DXRStateObject", "DXRShaderTable"};
	static_assert(names.size() == samples.size());
	for (size_t i = 0; i < samples.size(); ++i) {
		Logger::Output(LogType::Engine, "[EditorStartup] {} 回数={} CPU合計={:.1f}ms",
			names[i], samples[i].count, samples[i].milliseconds);
	}
}
