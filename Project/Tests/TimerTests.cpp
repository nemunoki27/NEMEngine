#include "TimerTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Time/TimerComponent.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Systems/Time/TimerSystem.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>

// c++
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}
}

bool NEMTests::TestTimer() {

	using namespace Engine;
	try {
		TimerComponent timer;
		UpdateTimer(timer, 1.0);
		Require(timer.runtimeSeconds == 0 && !timer.runtimePlaying, "既定では自動開始しない");
		timer.runtimeRequest = 1;
		UpdateTimer(timer, 0.25);
		Require(timer.runtimeSeconds == 0.25f && timer.runtimeFrames == 15, "秒数と基準フレーム数");
		timer.runtimeRequest = 2;
		UpdateTimer(timer, 1.0);
		Require(timer.runtimeSeconds == 0.25f && timer.runtimePaused, "一時停止中は進行しない");
		timer.runtimeRequest = 3;
		UpdateTimer(timer, 0.25);
		Require(timer.runtimeSeconds == 0.5f && !timer.runtimePaused, "続きから再開");
		timer.runtimeRequest = 4;
		UpdateTimer(timer, 1.0);
		Require(timer.runtimeSeconds == 0.5f && !timer.runtimePlaying, "停止は現在値を保持");
		timer.runtimeRequest = 1;
		UpdateTimer(timer, 0.125);
		Require(timer.runtimeSeconds == 0.125f, "開始は最初から");
		UpdateTimer(timer, 10.0);
		Require(timer.runtimeSeconds == 1 && timer.runtimeReached && !timer.runtimePlaying, "目標到達で停止");
		timer.runtimeRequest = 3;
		UpdateTimer(timer, 0.1);
		Require(!timer.runtimePlaying && timer.runtimeReached, "完了後の再開は無効");
		timer.runtimeRequest = 5;
		UpdateTimer(timer, 0);
		Require(timer.runtimeSeconds == 0 && !timer.runtimeReached && !timer.runtimePlaying, "リセットは停止状態");
		timer.targetSeconds = 0;
		timer.runtimeRequest = 1;
		UpdateTimer(timer, 0);
		Require(timer.runtimeReached && !timer.runtimePlaying, "目標0は即時完了");
		for (int fps : {30, 60, 120}) {
			TimerComponent seconds;
			seconds.playOnStart = true;
			for (int index = 0; index < fps; ++index) UpdateTimer(seconds, 1.0 / fps);
			Require(seconds.runtimeReached && seconds.runtimeSeconds == 1.0f, "秒指定は目標フレームで完了");
			TimerComponent frames;
			frames.mode = TimerMode::Frames;
			frames.targetFrames = 120;
			frames.runtimeRequest = 1;
			UpdateTimer(frames, 0);
			for (int index = 0; index < fps; ++index) UpdateTimer(frames, 1.0 / fps);
			Require(std::abs(frames.runtimeSeconds - 1.0f) < 0.0001f && frames.runtimeFrames == 60,
				"実FPSに依存せず小数フレームを積算");
			UpdateTimer(frames, 2.0);
			Require(frames.runtimeFrames == 120 && frames.runtimeReached, "フレーム指定の終端");
		}
		timer = {};
		timer.playOnStart = true;
		UpdateTimer(timer, 0.5);
		nlohmann::json saved = timer;
		const auto restored = saved.get<TimerComponent>();
		Require(restored.runtimeElapsed == 0 && !restored.runtimeInitialized && restored.playOnStart,
			"実行状態を保存しない");
		from_json(saved, timer);
		Require(timer.runtimeSeconds == 0.5f, "設定編集で実行状態を初期化しない");
		timer.runtimeRequest = 2;
		UpdateTimer(timer, 0);
		Require(timer.runtimePaused, "時間が停止していても操作を受け付ける");
		timer.runtimeRequest = 3;
		UpdateTimer(timer, std::numeric_limits<double>::quiet_NaN());
		Require(timer.runtimeSeconds == 0.5f, "不正な経過時間を加算しない");
		timer.baseFPS = 0;
		UpdateTimer(timer, 1);
		Require(!timer.runtimeDiagnostic.empty() && timer.runtimeSeconds == 0.5f, "不正設定は診断して進行停止");
		ECSWorld world;
		const Entity owner = world.CreateEntity();
		world.AddComponent<TimerComponent>(owner);
		world.AddComponent<TimeScaleComponent>(owner);
		auto& local = world.GetComponent<TimeScaleComponent>(owner);
		auto& running = world.GetComponent<TimerComponent>(owner);
		running.playOnStart = true;
		local.timeScale = 0.5f;
		TimerSystem system;
		SystemContext context;
		context.mode = WorldMode::Play;
		context.deltaTime = 0.5f;
		context.unscaledDeltaTime = 1.0f;
		system.Update(world, context);
		Require(running.runtimeSeconds == 0.25f, "Global時間にローカル時間を一度適用");
		local.timeScale = 0;
		system.Update(world, context);
		Require(running.runtimeSeconds == 0.25f, "ローカル時間0で停止");
		running.runtimeRequest = 5;
		system.Update(world, context);
		Require(running.runtimeSeconds == 0 && !running.runtimePlaying, "時間停止中にリセット");
		running.enabled = false;
		running.runtimeRequest = 1;
		system.Update(world, context);
		Require(!running.runtimePlaying, "無効なタイマーは更新しない");
		running.enabled = true;
		local.timeScale = 1;
		context.mode = WorldMode::Edit;
		system.Update(world, context);
		Require(!running.runtimePlaying, "編集時再生は既定で無効");
		running.playInEditMode = true;
		context.unscaledDeltaTime = 0.1f;
		system.Update(world, context);
		Require(running.runtimePlaying && std::abs(running.runtimeSeconds - 0.1f) < 0.0001f, "編集時の開始操作");
		nlohmann::json before;
		Require(world.SerializeComponentToJson(owner, "Timer", before), "Playへ渡す設定の保存");
		ECSWorld playWorld(ECSWorldKind::Runtime);
		const Entity playOwner = playWorld.CreateEntity();
		Require(playWorld.AddComponentFromJson(playOwner, "Timer", before), "新規PlayWorldへの設定復元");
		auto& playTimer = playWorld.GetComponent<TimerComponent>(playOwner);
		Require(playTimer.runtimeElapsed == 0 && !playTimer.runtimeInitialized && !playTimer.runtimePlaying,
			"編集時の経過と再生状態はPlayへ引き継がない");
		context.mode = WorldMode::Play;
		context.deltaTime = 0;
		system.Update(playWorld, context);
		Require(playTimer.runtimePlaying && playTimer.runtimeSeconds == 0, "PlayOnStartは新規PlayWorldで再評価");
		Require(std::abs(running.runtimeSeconds - 0.1f) < 0.0001f, "Play開始は編集Worldを変更しない");
		auto after = before;
		after["targetSeconds"] = 2.0f;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext commandContext;
		commandContext.editorContext = &editor;
		SetSerializedComponentCommand change(owner, "Timer", before, after);
		Require(change.Execute(commandContext) && running.targetSeconds == 2.0f && running.runtimePlaying &&
			std::abs(running.runtimeSeconds - 0.1f) < 0.0001f, "設定コマンドは経過時間を維持");
		change.Undo(commandContext);
		Require(running.targetSeconds == 1.0f && running.runtimePlaying &&
			std::abs(running.runtimeSeconds - 0.1f) < 0.0001f, "Undoは設定だけを復元");
		Require(change.Redo(commandContext) && running.targetSeconds == 2.0f && running.runtimePlaying &&
			std::abs(running.runtimeSeconds - 0.1f) < 0.0001f, "Redoは設定だけを再適用");
		std::cout << "[PASS] Timer\n";
		return true;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] Timer: " << error.what() << '\n';
		return false;
	}
}
