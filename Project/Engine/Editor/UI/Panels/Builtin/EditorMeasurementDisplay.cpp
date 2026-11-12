#include "EditorMeasurementDisplay.h"

//============================================================================
//	include
//============================================================================
#include "EditorGPUPassTooltip.h"
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Editor/Core/EditorState.h>

// c++
#include <algorithm>
#include <cstdio>

//============================================================================
//	EditorMeasurementDisplay functions
//============================================================================
void Engine::EditorMeasurementDisplay::UpdateHiddenData() {

	DrawEditorGPUPassTooltip(FrameProfiler::GetInstance(), false);
}

void Engine::EditorMeasurementDisplay::DrawData() {

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
	const Engine::FrameProfiler::ECSStatistics& ecsStatistics = profiler.GetECSStatistics();
	ImGui::Text("Entity数          : %u", ecsStatistics.entityCount);
	ImGui::Text("C#処理            : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Script));

	// CPUの全体時間とGPUのパス計測を分けて表示する
	ImGui::Text("描画全体CPU・待機込み : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::Draw));
	Engine::DrawEditorGPUPassTooltip(profiler, ImGui::IsItemHovered());
	ImGui::Text("  描画CPU・待機除外 : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::DrawCPU));
	ImGui::Text("    コマンド提出CPU : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::CommandSubmitCPU));
	if (ImGui::IsItemHovered()) {
		// API提出とGPUでの実行時間を混同しない
		ImGui::BeginTooltip();
		const auto& submission = profiler.GetRenderingStatistics();
		ImGui::Text("CommandListを閉じる : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::CommandCloseCPU));
		ImGui::Text("Buffer転送の提出 : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::BufferSubmitCPU));
		ImGui::Text("転送Context再利用待ち : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::UploadContextWaitCPU));
		ImGui::Text("提出回数 メイン %u / Buffer転送 %u", submission.graphicsSubmissionCount, submission.bufferSubmissionCount);
		ImGui::Text("同期API %u回 / 遷移 %u個 / UAV %u個", submission.barrierCallCount,
			submission.transitionBarrierCount, submission.uavBarrierCount);
		ImGui::TextUnformatted("CPU時間は平均、回数は直前フレームです。GPU実行時間とは加算しません");
		ImGui::TextUnformatted("提出回数はメイン描画とBuffer転送、同期回数はDxCommand経由の値です");
		ImGui::EndTooltip();
	}

	// Mesh更新のCPU時間を表示し、内訳と更新量はツールチップで確認する
	ImGui::Text("    Meshバッチ構築・転送 : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::MeshBatchUpload));
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

	// 待機を含む区間は描画全体の内訳として表示する
	ImGui::Text("  GPU完了待ち      : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::GPUWait));
	ImGui::Text("  画面表示CPU・同期待ち : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::PresentCPU));
	ImGui::Text("  FPS制限待機      : %.3f ms", profiler.GetAverageMs(Engine::FrameProfiler::Category::FPSWait));

	ImGui::Separator();

	const InputType inputType = Engine::Input::GetInstance()->GetType();
	ImGui::Text("入力デバイスタイプ: %s",
		inputType == InputType::GamePad ? "ゲームパッド" : "キーボード");
}

void Engine::EditorMeasurementDisplay::DrawOverlay(const EditorState& state, const ImVec2& position, const ImVec2& size, float topInset) {

	if ((!state.showSceneViewFPS && !state.showSceneViewDeltaTime) || size.x <= 16.0f || size.y <= 16.0f) {
		return;
	}
	const auto& profiler = FrameProfiler::GetInstance();
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(position, ImVec2(position.x + size.x, position.y + size.y), true);
	float y = position.y + 8.0f + topInset;
	auto line = [&](const char* text) {
		const float x = (std::max)(position.x + 8.0f, position.x + size.x - 8.0f - ImGui::CalcTextSize(text).x);
		draw->AddText(ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 255), text);
		draw->AddText(ImVec2(x, y), IM_COL32(0, 255, 0, 255), text);
		y += ImGui::GetTextLineHeightWithSpacing();
	};
	char text[64];
	if (state.showSceneViewFPS) {
		std::snprintf(text, sizeof(text), "FPS: %.1f", profiler.GetFPS());
		line(text);
	}
	if (state.showSceneViewDeltaTime) {
		std::snprintf(text, sizeof(text), "DeltaTime: %.3f ms", profiler.GetDeltaTimeSec() * 1000.0f);
		line(text);
	}
	draw->PopClipRect();
}
