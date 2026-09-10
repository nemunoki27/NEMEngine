#include "ScriptProfilerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <algorithm>
#include <set>

// imgui
#include <imgui.h>
#include <imgui_internal.h>

namespace Engine {

	void ScriptProfilerTool::OpenEditorTool() {
		openWindow_ = true;
	}

	void ScriptProfilerTool::Configure(bool enabled, const std::string& typeName, uint64_t ownerID) {
		ScriptProfiler::GetInstance().Configure(enabled, typeName, ownerID);
		rows_.clear();
		lastRefresh_ = -1;
	}

	void ScriptProfilerTool::RefreshRows() {
		auto& profiler = ScriptProfiler::GetInstance();
		rows_.clear();
		const auto& source = profiler.Rows();
		for (size_t i = 0; i < source.size(); ++i) {
			const auto& row = source[i];
			ViewRow view{};
			view.owner = row.owner;
			view.name = row.name;
			view.id = static_cast<int32_t>(i);
			view.parent = row.parent;
			view.detail = row.detail;
			view.grouped = row.grouped;
			const auto& latest = row.history[profiler.LastFrame()];
			view.latest = latest.inclusiveMs;
			view.self = latest.selfMs;
			view.calls = latest.calls;
			for (const auto& frame : row.history) {
				view.average += frame.inclusiveMs;
				view.maximum = std::max(view.maximum, frame.inclusiveMs);
				view.averageCalls += frame.calls;
			}
			const double count = std::max(1u, profiler.FrameCount());
			view.average /= count;
			view.averageCalls /= count;
			rows_.push_back(std::move(view));
		}
		std::stable_sort(rows_.begin(), rows_.end(), [this](const ViewRow& a, const ViewRow& b) {
			if (sort_ == 0) { return a.latest > b.latest; }
			if (sort_ == 2) { return a.maximum > b.maximum; }
			if (sort_ == 3) { return a.calls > b.calls; }
			return a.average > b.average;
		});
		detailChildren_.clear();
		for (size_t i = 0; i < rows_.size(); ++i) {
			if (rows_[i].detail) {
				detailChildren_[rows_[i].parent].push_back(i);
			}
		}
	}

	void ScriptProfilerTool::DrawRows(bool detail, int32_t parent) {
		const auto found = detailChildren_.find(parent);
		if (detail && found == detailChildren_.end()) { return; }
		const size_t count = detail ? found->second.size() : rows_.size();
		for (size_t i = 0; i < count; ++i) {
			const auto& row = rows_[detail ? found->second[i] : i];
			if (row.detail != detail || (detail && row.parent != parent) ||
				(!detail && row.grouped == instances_)) {
				continue;
			}
			if (!detail && filter_[0] && row.owner.typeName.find(filter_) == std::string::npos) {
				continue;
			}
			ImGui::PushID(row.id);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			bool opened = false;
			if (detail) {
				const bool hasChildren = detailChildren_.contains(row.id);
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
				if (!hasChildren) { flags |= ImGuiTreeNodeFlags_Leaf; }
				opened = ImGui::TreeNodeEx(row.name.c_str(), flags);
			} else {
				ImGui::TextUnformatted(row.owner.typeName.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(row.name.c_str());
				if (instances_) {
					ImGui::TextDisabled("World %u:%u / Entity %u:%u / Slot %llu",
						row.owner.entity.world.index, row.owner.entity.world.generation,
						row.owner.entity.index, row.owner.entity.generation,
						static_cast<unsigned long long>(row.owner.slotID));
				}
			}
			ImGui::TableSetColumnIndex(2); ImGui::Text("%.4f", row.latest);
			ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", row.average);
			ImGui::TableSetColumnIndex(4); ImGui::Text("%.4f", row.maximum);
			ImGui::TableSetColumnIndex(5); ImGui::Text("%.4f", row.self);
			ImGui::TableSetColumnIndex(6); ImGui::Text("%u / %.2f", row.calls, row.averageCalls);
			if (opened) {
				ImGui::TableSetColumnIndex(0);
				DrawRows(true, row.id);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	void ScriptProfilerTool::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {
		if (!openWindow_) {
			return;
		}
		constexpr const char* windowName = "スクリプトプロファイラー###Script Profiler";
		const ImGuiID windowID = ImHashStr(windowName);
		if (!ImGui::FindWindowByID(windowID) && !ImGui::FindWindowSettingsByID(windowID)) {
			// 表示名の変更前に保存された位置とドッキング先を引き継ぐ
			if (const auto* oldSettings = ImGui::FindWindowSettingsByID(ImHashStr("Script Profiler"))) {
				const ImGuiWindowSettings saved = *oldSettings;
				auto* settings = ImGui::CreateNewWindowSettings(windowName);
				*settings = saved;
				settings->ID = windowID;
				settings->WantApply = true;
				settings->WantDelete = false;
			}
		}
		if (!ImGui::Begin(windowName, &openWindow_)) {
			ImGui::End();
			return;
		}
		auto& profiler = ScriptProfiler::GetInstance();
		if (ImGui::Button(profiler.IsEnabled() ? "計測停止" : "計測開始")) {
			Configure(!profiler.IsEnabled(), profiler.DetailType(), profiler.DetailOwner());
		}
		ImGui::SameLine();
		if (ImGui::Button("クリア")) {
			profiler.Clear();
			rows_.clear();
			lastRefresh_ = -1;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("直近 %u / 300 フレーム", profiler.FrameCount());
		ImGui::TextWrapped("単位: ms。Native呼び出しを含むCPU時間。後段のコマンド適用・描画・GPU時間は含みません。");
		ImGui::TextWrapped("雨の通常更新はRainBlockManagerScript.LateUpdateに含まれます。詳細対象でControllerを選ぶと明示区間を確認できます。");

		std::set<std::string> types;
		for (const auto& [id, owner] : profiler.Owners()) {
			types.insert(owner.typeName);
		}
		if (ImGui::BeginCombo("詳細計測対象", profiler.DetailType().empty() ? "OFF" : profiler.DetailType().c_str())) {
			if (ImGui::Selectable("OFF", profiler.DetailType().empty())) {
				Configure(profiler.IsEnabled(), {}, 0);
			}
			for (const auto& type : types) {
				if (ImGui::Selectable(type.c_str(), profiler.DetailType() == type)) {
					Configure(profiler.IsEnabled(), type, 0);
				}
			}
			ImGui::EndCombo();
		}
		if (!profiler.DetailType().empty()) {
			if (ImGui::BeginCombo("インスタンス", profiler.DetailOwner() ? "個別選択中" : "同型すべて")) {
				if (ImGui::Selectable("同型すべて", profiler.DetailOwner() == 0)) {
					Configure(profiler.IsEnabled(), profiler.DetailType(), 0);
				}
				for (const auto& [id, owner] : profiler.Owners()) {
					if (owner.typeName != profiler.DetailType()) { continue; }
					const std::string label = "World " + std::to_string(owner.entity.world.index) +
						":" + std::to_string(owner.entity.world.generation) +
						" / Entity " + std::to_string(owner.entity.index) + ":" + std::to_string(owner.entity.generation) +
						" / Slot " + std::to_string(owner.slotID);
					if (ImGui::Selectable(label.c_str(), profiler.DetailOwner() == id)) {
						Configure(profiler.IsEnabled(), profiler.DetailType(), id);
					}
				}
				ImGui::EndCombo();
			}
		}
		ImGui::InputTextWithHint("##profileFilter", "型名で検索", filter_, sizeof(filter_));
		ImGui::Checkbox("Entity別に表示", &instances_);
		if (ImGui::Combo("降順", &sort_, "最新時間\0平均時間\0最大時間\0呼び出し回数\0")) {
			lastRefresh_ = -1;
		}
		if (lastRefresh_ < 0 || ImGui::GetTime() - lastRefresh_ >= 0.25) {
			RefreshRows();
			lastRefresh_ = ImGui::GetTime();
		}
		if (profiler.IsOverflowed()) {
			ImGui::TextWrapped("記録上限に達しました。最大4096行・ネスト128段。対象を絞ってクリアしてください。");
		}
		for (bool detail : { false, true }) {
			ImGui::SeparatorText(detail ? "選択スクリプトの詳細区間" : "コールバック一覧");
			if (detail) {
				ImGui::TextWrapped("子区間を含む時間と除いた時間を表示します。未計測の関数は自動分類しません。一覧と詳細を加算しないでください。");
			}
			if (ImGui::BeginTable(detail ? "details" : "callbacks", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
				for (const char* column : { "型 / 区間", "Callback / Entity", "最新ms", "平均ms", "最大ms", "最新Self ms", "回数 最新/平均" }) {
					ImGui::TableSetupColumn(column);
				}
				ImGui::TableHeadersRow();
				DrawRows(detail);
				ImGui::EndTable();
			}
		}
		ImGui::End();
	}
}
