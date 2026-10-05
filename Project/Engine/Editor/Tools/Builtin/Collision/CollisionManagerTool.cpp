#include "CollisionManagerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Utility/CollisionDebugDraw.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Settings/ProjectSettingsOperations.h>

// c++
#include <string>
// imgui
#include <imgui.h>

//============================================================================
//	CollisionManagerTool classMethods
//============================================================================

void Engine::CollisionManagerTool::Tick(ToolContext& context) {

	if (!CollisionSettings::GetInstance().GetDrawCollisionWorld() || !context.world) {
		return;
	}
	CollisionDebugDraw::DrawWorld(*context.world);
}

void Engine::CollisionManagerTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::CollisionManagerTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::CollisionManagerTool::DrawWindow([[maybe_unused]] const EditorToolContext& context) {

	if (!ImGui::Begin("衝突設定", &openWindow_)) {
		ImGui::End();
		return;
	}

	CollisionSettings& settings = CollisionSettings::GetInstance();
	settings.BindGlobal();
	settings.EnsureLoaded();

	ImGui::SetWindowFontScale(0.9f);

	// 全シーン共通のCollision設定ファイルを表示する
	const std::string settingsPath = settings.GetSettingsPath().generic_string();
	ImGui::TextDisabled("衝突設定ファイル: %s%s", settingsPath.c_str(), dirty_ ? " *" : "");

	// Save/Reloadボタン
	if (ImGui::Button("保存")) {
		// 保存失敗時は未保存表示を維持する
		if (settings.Save()) {
			dirty_ = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("読み込み##RELOAD")) {
		// 読込失敗時は編集中の設定を残す
		if (settings.Load()) {
			dirty_ = false;
		}
	}

	ImGui::Separator();

	// デバッグ用のCollision描画設定
	bool drawWorld = settings.GetDrawCollisionWorld();
	if (ImGui::Checkbox("衝突判定形状を全て描画", &drawWorld)) {
		settings.SetDrawCollisionWorld(drawWorld);
		dirty_ = true;
	}
	bool queriesHitTriggers = settings.GetQueriesHitTriggers();
	if (ImGui::Checkbox("物理クエリでTriggerを検出", &queriesHitTriggers)) {
		settings.SetQueriesHitTriggers(queriesHitTriggers);
		dirty_ = true;
	}
	ImGui::Separator();
	if (DrawTypes(context)) {
		dirty_ = true;
	}
	ImGui::Spacing();
	ImGui::Separator();
	if (DrawMatrix()) {
		dirty_ = true;
	}

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}

bool Engine::CollisionManagerTool::DrawTypes(const EditorToolContext& context) {

	CollisionSettings& settings = CollisionSettings::GetInstance();
	bool changed = false;

	if (!MyGUI::CollapsingHeader("衝突タイプ一覧")) {
		return changed;
	}

	const auto& types = settings.GetTypes();

	// Collisionタイプ名と有効状態を編集する
	for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		std::string name = types[i].name;
		if (MyGUI::InputText("名前", name).editFinished) {
			settings.SetTypeName(i, name);
			changed = true;
		}

		bool enabled = types[i].enabled;
		if (ImGui::Checkbox("有効", &enabled)) {
			settings.SetTypeEnabled(i, enabled);
			changed = true;
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (ImGui::Button("衝突タイプを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		settings.AddType("CollisionType" + std::to_string(settings.GetTypeCount()));
		changed = true;
	}

	// タイプが複数ある時だけ、コンボで選んだタイプを削除できるようにする
	if (settings.GetTypeCount() > 1) {

		// 一覧が縮んで選択が範囲外になっていたら先頭へ戻す
		if (removeTypeIndex_ >= static_cast<int32_t>(types.size())) {
			removeTypeIndex_ = 0;
		}

		// 削除ボタンの幅だけ余白を残してコンボを置く
		const float deleteButtonWidth = 60.0f;
		if (MyGUI::BeginPropertyRow("選択中のタイプを削除")) {

			const float comboWidth = ImGui::GetContentRegionAvail().x - (deleteButtonWidth + ImGui::GetStyle().ItemSpacing.x);
			ImGui::SetNextItemWidth(comboWidth <= 1.0f ? 1.0f : comboWidth);

			if (ImGui::BeginCombo("##RemoveTarget", types[removeTypeIndex_].name.c_str())) {
				for (uint32_t i = 0; i < static_cast<uint32_t>(types.size()); ++i) {

					const bool selected = (removeTypeIndex_ == static_cast<int32_t>(i));
					if (ImGui::Selectable(types[i].name.c_str(), selected)) {
						removeTypeIndex_ = static_cast<int32_t>(i);
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (ImGui::Button("削除", ImVec2(deleteButtonWidth, 0.0f))) {
				const uint32_t removeIndex = static_cast<uint32_t>(removeTypeIndex_);
				if (ProjectSettingsOperations::RemoveCollisionType(context, removeIndex)) {
					settings.RemoveType(removeIndex);
					removeTypeIndex_ = 0;
					changed = true;
				}
			}
			MyGUI::EndPropertyRow();
		}
	}
	return changed;
}

bool Engine::CollisionManagerTool::DrawMatrix() {

	CollisionSettings& settings = CollisionSettings::GetInstance();
	const auto& types = settings.GetTypes();
	const uint32_t count = static_cast<uint32_t>(types.size());
	bool changed = false;

	if (!MyGUI::CollapsingHeader("衝突レイヤーマトリックス")) {
		return changed;
	}
	if (count == 0) {
		ImGui::TextDisabled("衝突タイプが設定されていません");
		return changed;
	}

	const ImGuiTableFlags flags =
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
	if (!ImGui::BeginTable("##CollisionMatrix", static_cast<int32_t>(count + 1), flags)) {
		return changed;
	}

	ImGui::TableSetupColumn("タイプ");
	for (uint32_t i = 0; i < count; ++i) {
		ImGui::TableSetupColumn(types[i].name.c_str());
	}
	ImGui::TableHeadersRow();

	// UnityのLayer Collision Matrixに近い見た目で衝突可否を編集する
	for (uint32_t y = 0; y < count; ++y) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(types[y].name.c_str());

		for (uint32_t x = 0; x < count; ++x) {

			ImGui::TableSetColumnIndex(static_cast<int32_t>(x + 1));
			ImGui::PushID(static_cast<int32_t>(y * kMaxCollisionTypes + x));
			bool enabled = settings.IsPairEnabled(y, x);
			if (ImGui::Checkbox("##Pair", &enabled)) {
				settings.SetPairEnabled(y, x, enabled);
				changed = true;
			}
			ImGui::PopID();
		}
	}

	ImGui::EndTable();
	return changed;
}
