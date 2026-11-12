#include "TimelineTool.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/Commands/Components/AddComponentCommand.h>
#include <Engine/Editor/UI/Common/EditorIconButton.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

using namespace Engine;

void TimelineTool::DrawToolbar(const EditorToolContext& context) {

	const float size = ImGui::GetFrameHeight();
	const bool hasOwner = context.GetWorld() && context.GetWorld()->IsAlive(session_.GetOwner(context));
	const bool playback = session_.HasAsset() && hasOwner;
	const float available = ImGui::GetContentRegionAvail().x;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 2.0f));
	ImGui::BeginDisabled(!playback);
	if (available > 620.0f) {
		const bool selected = session_.previewEnabled;
		EditorIconButton::PushColors(selected);
		const float width = (std::max)(82.0f, ImGui::CalcTextSize("プレビュー").x + ImGui::GetStyle().FramePadding.x * 2.0f);
		const bool clicked = ImGui::Button("プレビュー", ImVec2(width, size));
		EditorIconButton::PopColors();
		if (clicked) {
			session_.previewEnabled = !session_.previewEnabled;
			if (!session_.previewEnabled) session_.StopPlayback(context);
		}
	} else if (EditorIconButton::Draw("Preview", "\xef\x81\xae", "プレビュー", session_.previewEnabled, size)) {
		session_.previewEnabled = !session_.previewEnabled;
		if (!session_.previewEnabled) session_.StopPlayback(context);
	}
	ImGui::SameLine();
	if (available > 520.0f) {
		if (EditorIconButton::Draw("First", "\xef\x81\x88", "先頭へ Shift+,", false, size)) session_.Seek(context, 0.0);
		ImGui::SameLine();
		if (EditorIconButton::Draw("Previous", "\xef\x81\x93", "前のフレーム ,", false, size)) session_.Step(context, -1);
		ImGui::SameLine();
	}
	if (EditorIconButton::Draw("Play", session_.playing ? "\xef\x81\x8c" : "\xef\x81\x8b",
		session_.playing ? "一時停止 Space" : "再生 Space", session_.playing, size)) session_.TogglePlayback(context);
	ImGui::SameLine();
	if (available > 520.0f) {
		if (EditorIconButton::Draw("Next", "\xef\x81\x94", "次のフレーム .", false, size)) session_.Step(context, 1);
		ImGui::SameLine();
		if (EditorIconButton::Draw("Last", "\xef\x81\x91", "末尾へ Shift+.", false, size)) session_.Seek(context, session_.GetAsset().duration);
		ImGui::SameLine();
		if (EditorIconButton::Draw("Stop", "\xef\x81\x8d", "停止してプレビュー前の状態へ戻す", false, size)) session_.StopPlayback(context);
		ImGui::SameLine();
	}
	if (available > 440.0f) {
		ImGui::BeginDisabled(context.IsPlaying());
		if (EditorIconButton::Draw("Range", "\xef\x8d\xa3", "指定範囲をループ確認", session_.rangeEnabled, size)) {
			session_.rangeEnabled = !session_.rangeEnabled;
			if (session_.rangeEnd <= session_.rangeStart) session_.rangeEnd = session_.GetAsset().duration;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
	}
	const char* sample = timeDisplay_ == TimeDisplay::Timecode ? "00:00:00" : timeDisplay_ == TimeDisplay::Seconds ? "000.000" : "000000";
	const float timeWidth = ImGui::CalcTextSize(sample).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SetNextItemWidth((std::max)(timeWidth, available < 620.0f ? 58.0f : 78.0f));
	const auto timeColors = EditorIconButton::GetColors(false);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, timeColors.normal);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, timeColors.hovered);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, timeColors.pressed);
	if (timeDisplay_ == TimeDisplay::Timecode) {
		if (!timecodeEditing_) timecodeEdit_ = FormatTime(session_.time);
		const bool submitted = ImGui::InputText("##Time", &timecodeEdit_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		timecodeEditing_ = ImGui::IsItemActive();
		if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
			int minutes = 0, seconds = 0, frame = 0;
			if (::sscanf_s(timecodeEdit_.c_str(), "%d:%d:%d", &minutes, &seconds, &frame) == 3 &&
				minutes >= 0 && seconds >= 0 && seconds < 60 && frame >= 0 && frame < session_.GetAsset().frameRate)
				session_.Seek(context, minutes * 60.0 + seconds + frame / session_.GetAsset().frameRate);
		}
	} else {
		double time = timeDisplay_ == TimeDisplay::Seconds ? session_.time : std::round(session_.time * session_.GetAsset().frameRate);
		if (ImGui::DragScalar("##Time", ImGuiDataType_Double, &time, timeDisplay_ == TimeDisplay::Seconds ? 0.01f : 1.0f,
			nullptr, nullptr, timeDisplay_ == TimeDisplay::Seconds ? "%.3f" : "%.0f"))
			session_.Seek(context, timeDisplay_ == TimeDisplay::Seconds ? time : time / session_.GetAsset().frameRate);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("現在位置を指定 タイムコードは 分:秒:フレーム");
	ImGui::PopStyleColor(3);
	ImGui::EndDisabled();
	ImGui::SameLine();
	DrawTimelineSelector(context, (std::max)(32.0f, ImGui::GetContentRegionAvail().x - size * (available < 340.0f ? 1.0f : 3.0f) - 4.0f));
	ImGui::SameLine();
	if (available >= 340.0f) {
		if (EditorIconButton::Draw("TargetLock", targetLocked_ ? "\xef\x80\xa3" : "\xef\x82\x9c",
			"編集対象を固定", targetLocked_, size)) targetLocked_ = !targetLocked_;
		ImGui::SameLine();
	}
	const bool warning = !session_.GetError().empty() || !session_.GetPreviewDiagnostic().empty() || !recording_.GetError().empty();
	if (EditorIconButton::Draw("File", warning ? "\xef\x81\xb1" : "\xef\x85\x82",
		warning ? "タイムラインの診断を確認" : "タイムラインのメニュー", false, size)) ImGui::OpenPopup("TimelineFile");
	if (ImGui::BeginPopup("TimelineFile")) { DrawFileMenu(context); ImGui::EndPopup(); }
	if (available >= 340.0f) {
		ImGui::SameLine();
		if (EditorIconButton::Draw("Options", "\xef\x80\x93", "表示とタイムラインの設定", false, size)) ImGui::OpenPopup("TimelineSettings");
	}
	if (ImGui::BeginPopup("TimelineSettings")) { DrawSettingsMenu(); ImGui::EndPopup(); }
	ImGui::PopStyleVar();
	creationDialog_.Draw("タイムラインを作成", ProjectAssetFileKind::Timeline,
		[&](const std::string& path, std::string& diagnostic) {
			if (HasPendingEdits()) { RequestAction(context, { PendingKind::Create, {}, path }); return true; }
			const bool success = ExecuteAction(context, { PendingKind::Create, {}, path });
			diagnostic = session_.GetError();
			return success;
		});
}

void TimelineTool::DrawTimelineSelector(const EditorToolContext& context, float width) {

	std::string caption = session_.HasAsset() ? session_.GetAsset().name : "タイムラインを選択";
	if (context.GetWorld() && context.GetWorld()->IsAlive(session_.GetOwner(context)))
		caption += " (" + GetEntityDisplayName(*context.GetWorld(), session_.GetOwner(context)) + ")";
	if (HasPendingEdits()) caption += " *";
	ImGui::SetNextItemWidth(width);
	if (!ImGui::BeginCombo("##TimelineSelector", caption.c_str())) return;
	if (session_.HasAsset() && ImGui::Selectable("タイムラインの設定をInspectorに表示")) SelectInspection(context);
	if (ECSWorld* world = context.GetWorld()) {
		ImGui::SeparatorText("シーン内の再生対象");
		std::vector<std::pair<std::string, UUID>> players;
		world->ForEach<TimelinePlayerComponent>([&](Entity entity, const TimelinePlayerComponent&) {
			if (const auto* player = TryGetTimelinePlayer(*world, entity); player && player->settings.timeline)
				players.emplace_back(GetEntityDisplayName(*world, entity), world->GetUUID(entity));
			});
		std::sort(players.begin(), players.end(), [](const auto& first, const auto& second) { return first.first < second.first; });
		for (const auto& [name, id] : players) if (ImGui::Selectable(name.c_str(), session_.GetOwnerID() == id)) {
			const Entity entity = world->FindByUUID(id);
			const auto* player = TryGetTimelinePlayer(*world, entity);
			OpenPlayer(player->settings.timeline, id);
			if (context.panelContext && context.panelContext->editorState) context.panelContext->editorState->SelectEntity(entity);
		}
	}
	if (const auto* database = context.toolContext.assetDatabase) {
		ImGui::SeparatorText("タイムラインアセット");
		std::vector<std::pair<std::string, AssetID>> assets;
		for (const auto& [id, meta] : database->GetAssets()) if (meta.type == AssetType::Timeline)
			assets.emplace_back(meta.assetPath, id);
		std::sort(assets.begin(), assets.end(), [](const auto& first, const auto& second) { return first.first < second.first; });
		for (const auto& [path, id] : assets) if (ImGui::Selectable(path.c_str(), session_.GetAssetID() == id)) {
			queuedOwner_ = {};
			RequestAction(context, { PendingKind::Load, id, {}, true });
		}
	}
	ImGui::EndCombo();
}

void TimelineTool::DrawFileMenu(const EditorToolContext& context) {

	const auto referenceRow = [](const char* label) {
		return PropertyRowSetting{
			.labelWidth = ImGui::CalcTextSize(label).x + 4.0f,
			.rowWidth = (std::max)(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 24.0f)
		};
	};
	if (ImGui::MenuItem("新規作成")) creationDialog_.Open();
	ImGui::BeginDisabled(!session_.HasAsset());
	if (ImGui::MenuItem(HasPendingEdits() ? "保存 *" : "保存", "Ctrl+S")) SaveAll(context);
	if (ImGui::MenuItem("タイムラインの設定")) SelectInspection(context);
	if (ImGui::BeginMenu("設定をインポート")) {
		AssetID source{};
		if (MyGUI::AssetReferenceField("設定をインポート", source, context.toolContext.assetDatabase,
			{ AssetType::Timeline }, { .allowDelete = false, .propertyRow = referenceRow("設定をインポート") }).valueChanged)
			RequestAction(context, { PendingKind::Import, source });
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("再生")) {
		const bool alive = context.GetWorld() && context.GetWorld()->IsAlive(session_.GetOwner(context));
		ImGui::BeginDisabled(!alive);
		if (ImGui::MenuItem(session_.playing ? "一時停止" : "再生", "Space")) session_.TogglePlayback(context);
		if (ImGui::MenuItem("停止")) session_.StopPlayback(context);
		if (ImGui::MenuItem("先頭へ", "Shift+,")) session_.Seek(context, 0.0);
		if (ImGui::MenuItem("前のフレーム", ",")) session_.Step(context, -1);
		if (ImGui::MenuItem("次のフレーム", ".")) session_.Step(context, 1);
		if (ImGui::MenuItem("末尾へ", "Shift+.")) session_.Seek(context, session_.GetAsset().duration);
		ImGui::BeginDisabled(context.IsPlaying());
		ImGui::MenuItem("指定範囲をループ確認", nullptr, &session_.rangeEnabled);
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("表示とタイムラインの設定")) { DrawSettingsMenu(); ImGui::EndMenu(); }
	ImGui::MenuItem("編集対象を固定", nullptr, &targetLocked_);
	ImGui::Separator();
	UUID owner = session_.GetOwnerID();
	if (MyGUI::EntityReferenceField("再生対象", owner, context.GetWorld(),
		{ .propertyRow = referenceRow("再生対象") }).valueChanged) {
		ResetInteraction();
		session_.SetOwner(context, owner);
		if (context.panelContext && context.panelContext->editorState) {
			if (owner) context.panelContext->editorState->SelectEntity(context.GetWorld()->FindByUUID(owner));
			else context.panelContext->editorState->SelectAsset(session_.GetAssetID());
		}
	}
	const Entity entity = session_.GetOwner(context);
	const bool alive = context.GetWorld() && context.GetWorld()->IsAlive(entity);
	ImGui::BeginDisabled(!alive);
	if (alive && !TryGetTimelinePlayer(*context.GetWorld(), entity)) {
		if (ImGui::MenuItem("TimelinePlayerを追加") && context.panelContext && context.panelContext->host) {
			context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(entity, "TimelinePlayer"));
			session_.SaveBindings(context);
		}
	} else if (ImGui::MenuItem("割り当てをSceneへ保存")) session_.SaveBindings(context);
	ImGui::EndDisabled();
	ImGui::EndDisabled();
	const std::string& diagnostic = !recording_.GetError().empty() ? recording_.GetError() :
		session_.GetError().empty() ? session_.GetPreviewDiagnostic() : session_.GetError();
	if (!diagnostic.empty() && ImGui::BeginMenu("診断")) { ImGui::TextWrapped("%s", diagnostic.c_str()); ImGui::EndMenu(); }
}

void TimelineTool::DrawSettingsMenu() {

	ImGui::SeparatorText("時間表示");
	const char* labels[]{ "フレーム", "秒", "タイムコード" };
	for (int i = 0; i < 3; ++i) if (ImGui::MenuItem(labels[i], nullptr, static_cast<int>(timeDisplay_) == i)) {
		timeDisplay_ = static_cast<TimeDisplay>(i);
		frames_ = timeDisplay_ != TimeDisplay::Seconds;
	}
	ImGui::Separator();
	ImGui::MenuItem("フレームスナップ", nullptr, &snap_);
	ImGui::MenuItem("再生位置へ表示を追従", nullptr, &followPlayhead_);
	ImGui::MenuItem("イベントを表示", nullptr, &showEvents_);
	ImGui::MenuItem("重なりを自動ブレンド", nullptr, &autoBlend_);
	if (!session_.HasAsset()) return;
	ImGui::SeparatorText("タイムライン");
	auto& asset = session_.GetAsset();
	if (ImGui::MenuItem("固定長", nullptr, asset.durationMode == TimelineDurationMode::FixedLength)) {
		asset.durationMode = TimelineDurationMode::FixedLength;
		session_.MarkChanged();
	}
	if (ImGui::MenuItem("クリップ末尾に追従", nullptr, asset.durationMode == TimelineDurationMode::BasedOnClips)) {
		asset.durationMode = TimelineDurationMode::BasedOnClips;
		TimelineEditOperations::UpdateDuration(asset);
		session_.MarkChanged();
	}
	if (ImGui::BeginMenu("基準FPS")) {
		for (const double fps : {24.0, 25.0, 30.0, 50.0, 60.0, 120.0}) {
			const std::string name = std::format("{:.0f}", fps);
			if (ImGui::MenuItem(name.c_str(), nullptr, asset.frameRate == fps)) { asset.frameRate = fps; session_.MarkChanged(); }
		}
		ImGui::EndMenu();
	}
}
