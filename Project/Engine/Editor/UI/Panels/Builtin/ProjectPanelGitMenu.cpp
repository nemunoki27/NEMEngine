#include "ProjectPanel.h"

void Engine::ProjectPanel::DrawGitContextMenu(const std::filesystem::path& path, bool directory) {

	const auto* state = gitIgnore_.Request(path, directory);
	if (!state) {
		ImGui::TextDisabled("Gitの状態を確認中...");
		return;
	}
	if (!state->valid) {
		ImGui::TextDisabled("%s", state->message.c_str());
		return;
	}
	// 親の除外を個別のアセットから解除しない
	const bool include = state->excluded;
	const bool enabled = !gitIgnore_.IsBusy() && !(include && state->parentExcluded);
	if (ImGui::MenuItem(include ? "Git管理に含める" : "Git管理から外す", nullptr, false, enabled)) {
		gitIgnore_.SetIncluded(include);
	}
	if (state->parentExcluded) {
		ImGui::TextDisabled("親フォルダーがGit管理から除外されています");
	} else if (state->tracked) {
		ImGui::TextDisabled("追跡済みファイルの索引変更は手動で行います");
	}
	if (gitIgnore_.IsBusy()) { ImGui::TextDisabled("Git設定を処理中..."); }
	if (!state->message.empty()) { ImGui::TextDisabled("%s", state->message.c_str()); }
	ImGui::Separator();
}
