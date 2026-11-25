#include "EditorPalette.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Utility/EditorSourceNavigation.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <ranges>
#include <set>
#include <utility>

using namespace Engine;

namespace {

	using Json = nlohmann::json;

	struct Entry {
		std::string label;
		std::string group;
		ImVec4 base{};
		ImVec4 value{};
		bool scalar = false;
		float minimum = 0;
		float maximum = 1;
	};

	using Entries = std::map<std::string, Entry, std::less<>>;

	bool Decode(const Json& data, const Entry& entry, ImVec4& result) {
		if (entry.scalar ? !data.is_number() : (!data.is_array() || data.size() != 4)) return false;
		ImVec4 parsed{};
		for (int i = 0; i < (entry.scalar ? 1 : 4); ++i) {
			const auto& component = entry.scalar ? data : data[i];
			if (!component.is_number()) return false;
			const auto value = component.get<double>();
			if (!std::isfinite(value) || value < entry.minimum || entry.maximum < value) return false;
			(&parsed.x)[i] = static_cast<float>(value);
		}
		result = parsed;
		return true;
	}

	bool Changed(const Entry& entry) {
		return entry.value.x != entry.base.x || entry.value.y != entry.base.y ||
			entry.value.z != entry.base.z || entry.value.w != entry.base.w;
	}

	Json Encode(const Entry& entry) {
		const auto& v = entry.value;
		return entry.scalar ? Json(v.x) : Json::array({v.x, v.y, v.z, v.w});
	}

	Json ReadObject(const std::filesystem::path& path) {
		Json data;
		if (!path.empty() && JsonFile::TryLoad(path, data) && data.is_object()) return data;
		return Json::object();
	}

	struct Palette {
		Entries entries;
		std::map<std::string, std::vector<std::string>> groups;
		std::vector<std::string> groupOrder;
		std::set<std::string> dirty;
		std::filesystem::path defaultsPath;
		std::filesystem::path userPath;
		unsigned long long revision = 1;

		Palette(const std::filesystem::path& directory, const std::filesystem::path& userFile)
			: defaultsPath(directory / "EngineDefaults.json"), userPath(userFile) {

			std::error_code ec;
			for (const auto& file : std::filesystem::directory_iterator(directory, ec)) {
				if (file.path().extension() != ".json" || file.path() == defaultsPath) continue;
				const auto catalog = ReadObject(file.path());
				for (const auto& [key, row] : catalog.items()) {
					if (!row.is_object() || !row.contains("label") || !row["label"].is_string() ||
						!row.contains("group") || !row["group"].is_string() || !row.contains("value")) continue;
					Entry entry;
					entry.label = row["label"].get<std::string>();
					entry.group = row["group"].get<std::string>();
					entry.scalar = key.starts_with("Style.");
					if (entry.scalar) {
						if (!row.contains("min") || !row["min"].is_number() || !row.contains("max") || !row["max"].is_number()) continue;
						entry.minimum = row["min"].get<float>();
						entry.maximum = row["max"].get<float>();
					}
					if (Decode(row["value"], entry, entry.base)) entries.emplace(key, std::move(entry));
				}
			}
			const auto defaults = ReadObject(defaultsPath);
			const auto overrides = ReadObject(userPath);
			for (auto& [key, entry] : entries) {
				if (defaults.contains(key)) Decode(defaults.at(key), entry, entry.base);
				entry.value = entry.base;
				if (overrides.contains(key)) Decode(overrides.at(key), entry, entry.value);
				groups[entry.group].push_back(key);
			}
			for (const auto& [group, keys] : groups) groupOrder.push_back(group);
			const auto priority = [](const std::string& group) {
				if (group == "基本UI・文字と操作表示") return 0;
				if (group.starts_with("基本UI")) return 1;
				if (group == "パネル・ツールバー") return 2;
				return 3;
			};
			std::stable_sort(groupOrder.begin(), groupOrder.end(), [&](const auto& a, const auto& b) {
				return priority(a) < priority(b);
			});
		}
	};

	Palette& State() {
		static Palette palette = [] {
			std::filesystem::path userFile;
			wchar_t* local = nullptr;
			size_t length = 0;
			if (_wdupenv_s(&local, &length, L"LOCALAPPDATA") == 0 && local && *local) {
				userFile = std::filesystem::path(local) / "NEMEngine/Editor/ColorOverrides.json";
			}
			std::free(local);
			return Palette(RuntimePaths::GetEngineAssetPath("Config/EditorPalette"), userFile);
		}();
		return palette;
	}

}

bool Engine::EditorPalette::SaveDefaults() {
	if (!EditorSourceNavigation::IsEngineSourceProject()) return false;
	auto& palette = State();
	auto data = ReadObject(palette.defaultsPath);
	for (const auto& [key, entry] : palette.entries) data[key] = Encode(entry);
	if (!JsonFile::SaveCanonical(palette.defaultsPath, data, 2)) return false;
	for (auto& [key, entry] : palette.entries) {
		if (Changed(entry)) palette.dirty.insert(key);
		entry.base = entry.value;
	}
	return EditorPalette::SaveUser();
}

bool Engine::EditorPalette::Load(const std::filesystem::path& catalogDirectory, const std::filesystem::path& userFile) {

	Palette loaded(catalogDirectory, userFile);
	if (loaded.entries.empty()) return false;
	auto& palette = State();
	loaded.revision = palette.revision + 1;
	palette = std::move(loaded);
	return true;
}

ImVec4 Engine::EditorPalette::Get(std::string_view key) {
	const auto& entries = State().entries;
	const auto it = entries.find(key);
	return it == entries.end() ? ImVec4{} : it->second.value;
}

ImU32 Engine::EditorPalette::U32(std::string_view key) {
	return ImGui::ColorConvertFloat4ToU32(Get(key));
}

Color4 Engine::EditorPalette::RGBA(std::string_view key) {
	const auto c = Get(key);
	return {c.x, c.y, c.z, c.w};
}

void Engine::EditorPalette::Set(std::string_view key, ImVec4 value) {
	auto& palette = State();
	const auto it = palette.entries.find(key);
	if (it == palette.entries.end()) return;
	auto& entry = it->second;
	const auto& old = entry.value;
	if (old.x == value.x && old.y == value.y && old.z == value.z && old.w == value.w) return;
	entry.value = value;
	palette.dirty.insert(it->first);
	++palette.revision;
}

unsigned long long Engine::EditorPalette::Revision() {
	return State().revision;
}

bool Engine::EditorPalette::SaveUser() {
	auto& palette = State();
	if (palette.dirty.empty()) return true;
	if (palette.userPath.empty()) return false;
	// 他のバージョンが追加した項目を保持する
	auto data = ReadObject(palette.userPath);
	for (const auto& key : palette.dirty) {
		const auto& entry = palette.entries.at(key);
		if (Changed(entry)) data[key] = Encode(entry);
		else data.erase(key);
	}
	if (!JsonFile::SaveCanonical(palette.userPath, data, 2)) return false;
	palette.dirty.clear();
	return true;
}

void Engine::EditorPalette::Apply(ImGuiStyle& style, bool platformWindows) {
	const auto& entries = State().entries;
	for (int i = 0; i < ImGuiCol_COUNT; ++i) {
		const auto it = entries.find(std::string("ImGui.") + ImGui::GetStyleColorName(i));
		if (it != entries.end()) style.Colors[i] = it->second.value;
	}
	if (!entries.contains("Style.WindowRounding")) return;
	style.WindowRounding = Get("Style.WindowRounding").x;
	style.ChildRounding = Get("Style.ChildRounding").x;
	style.FrameRounding = Get("Style.FrameRounding").x;
	style.ScrollbarRounding = Get("Style.ScrollbarRounding").x;
	style.GrabRounding = Get("Style.GrabRounding").x;
	style.TabRounding = Get("Style.TabRounding").x;
	style.TabBorderSize = Get("Style.TabBorderSize").x;
	style.TabBarBorderSize = Get("Style.TabBarBorderSize").x;
	style.TabBarOverlineSize = Get("Style.TabBarOverlineSize").x;
	style.TabCloseButtonMinWidthSelected = Get("Style.TabCloseButtonMinWidthSelected").x;
	style.TabCloseButtonMinWidthUnselected = Get("Style.TabCloseButtonMinWidthUnselected").x;
	style.WindowBorderSize = Get("Style.WindowBorderSize").x;
	style.FrameBorderSize = Get("Style.FrameBorderSize").x;
	style.DockingSeparatorSize = Get("Style.DockingSeparatorSize").x;
	style.FramePadding = {Get("Style.FramePaddingX").x, Get("Style.FramePaddingY").x};
	style.WindowTitleAlign = {Get("Style.WindowTitleAlignX").x, Get("Style.WindowTitleAlignY").x};
	style.WindowMenuButtonPosition = static_cast<ImGuiDir>(static_cast<int>(Get("Style.WindowMenuButtonPosition").x));
	style.DockingNodeHasCloseButton = Get("Style.DockingNodeHasCloseButton").x != 0;
	if (platformWindows) {
		style.WindowRounding = Get("Style.PlatformWindowRounding").x;
		style.Colors[ImGuiCol_WindowBg].w *= Get("Style.PlatformBackgroundAlpha").x;
	}
}

namespace {

	void DrawValue(const std::string& key, const Entry& entry) {
		ImVec4 value = entry.value;
		bool edited = false;
		ImGui::SetNextItemWidth(-1.0f);
		if (!entry.scalar) {
			edited = ImGui::ColorEdit4("##Value", &value.x, ImGuiColorEditFlags_DisplayHex |
				ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
		} else if (key == "Style.DockingNodeHasCloseButton") {
			bool enabled = value.x != 0;
			edited = ImGui::Checkbox("##Value", &enabled);
			value.x = enabled ? 1.0f : 0.0f;
		} else if (key == "Style.WindowMenuButtonPosition") {
			int index = static_cast<int>(value.x) + 1;
			edited = ImGui::Combo("##Value", &index, "なし\0左\0右\0");
			value.x = static_cast<float>(index - 1);
		} else {
			edited = ImGui::DragFloat("##Value", &value.x, entry.maximum <= 1 ? 0.01f : 0.1f,
				entry.minimum, entry.maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (key.starts_with("Style.TabCloseButtonMinWidth") && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("-1で常に表示、0でホバー時に表示、正の値で最小タブ幅を指定");
			}
		}
		if (edited) EditorPalette::Set(key, value);
	}

	void DrawEntries(bool dimensions, const ImGuiTextFilter& search, bool changedOnly) {
		const auto& palette = State();
		bool matched = false;
		for (const auto& group : palette.groupOrder) {
			const auto& keys = palette.groups.at(group);
			auto rows = keys | std::views::filter([&](const std::string& key) {
				const auto& entry = palette.entries.at(key);
				return entry.scalar == dimensions && (!changedOnly || Changed(entry)) &&
					(!search.IsActive() || search.PassFilter(entry.label.c_str()) ||
						search.PassFilter(key.c_str()) || search.PassFilter(group.c_str()));
			});
			if (rows.empty()) continue;
			matched = true;
			if (search.IsActive()) ImGui::SetNextItemOpen(true);
			const bool defaultOpen = group == "基本UI・文字と操作表示" || dimensions;
			if (!ImGui::CollapsingHeader(group.c_str(), defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
			ImGui::PushID(group.c_str());
			if (!ImGui::BeginTable("項目", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
				ImGui::PopID();
				continue;
			}
			ImGui::TableSetupColumn("項目名", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn("編集", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("復元", ImGuiTableColumnFlags_WidthFixed,
				ImGui::CalcTextSize("既定に戻す").x + ImGui::GetFrameHeight());
			for (const auto& key : rows) {
				const auto& entry = palette.entries.at(key);
				ImGui::PushID(key.c_str());
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextWrapped("%s", entry.label.c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", key.c_str());
				ImGui::TableNextColumn();
				DrawValue(key, entry);
				ImGui::TableNextColumn();
				if (Changed(entry) && ImGui::SmallButton("既定に戻す")) EditorPalette::Set(key, entry.base);
				ImGui::PopID();
			}
			ImGui::EndTable();
			ImGui::PopID();
		}
		if (!matched) ImGui::TextDisabled("表示条件に一致する項目がありません");
	}
}

void Engine::EditorPalette::DrawSettings(bool& open) {
	if (!open) return;
	static ImGuiTextFilter search;
	static bool changedOnly = false;
	static std::string error;
	ImGui::SetNextWindowSize(ImVec2(680, 620), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("配色設定", &open)) {
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
		if (ImGui::InputTextWithHint("##SearchColors", "項目を検索...", search.InputBuf, sizeof(search.InputBuf))) search.Build();
		ImGui::SameLine();
		ImGui::Checkbox("変更した項目のみ", &changedOnly);
		if (ImGui::Button("すべて既定に戻す")) ImGui::OpenPopup("個人設定のリセット");
		if (EditorSourceNavigation::IsEngineSourceProject()) {
			ImGui::SameLine();
			if (ImGui::Button("現在の配色をエンジン既定に保存")) ImGui::OpenPopup("配布用の既定設定");
		}
		if (ImGui::BeginPopupModal("個人設定のリセット", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("配色と寸法を、このエンジンの既定値に戻しますか？");
			if (ImGui::Button("既定に戻す")) {
				for (const auto& [key, entry] : State().entries) Set(key, entry.base);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("配布用の既定設定", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("現在の配色と寸法を、次回配布するエンジンの既定値にしますか？");
			if (ImGui::Button("エンジン既定に保存")) {
				error = SaveDefaults() ? "" : "設定の保存に失敗しました";
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
		if (ImGui::BeginTabBar("設定の種類")) {
			for (const bool dimensions : {false, true}) {
				if (ImGui::BeginTabItem(dimensions ? "寸法・表示" : "配色")) {
					ImGui::BeginChild("一覧");
					DrawEntries(dimensions, search, changedOnly);
					ImGui::EndChild();
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
	if (!State().dirty.empty() && !ImGui::IsAnyItemActive()) {
		error = SaveUser() ? "" : "個人設定を保存できませんでした";
	}
}
