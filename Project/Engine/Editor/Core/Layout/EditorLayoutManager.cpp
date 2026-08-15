#include "EditorLayoutManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <cctype>
#include <unordered_set>

//============================================================================
//	EditorLayoutManager classMethods
//============================================================================
namespace {

	constexpr int32_t kSchemaVersion = 1;

	nlohmann::json MakeVisibilityJson(const Engine::EditorPanelVisibility& visibility) {

		return {
			{ "hierarchy", visibility.showHierarchy },
			{ "inspector", visibility.showInspector },
			{ "project", visibility.showProject },
			{ "console", visibility.showConsole },
			{ "sceneView", visibility.showSceneView },
			{ "gameView", visibility.showGameView },
			{ "toolbar", visibility.showToolbar },
			{ "tool", visibility.showTool },
		};
	}

	Engine::EditorPanelVisibility ReadVisibility(const nlohmann::json& data) {

		Engine::EditorPanelVisibility visibility{};
		if (!data.is_object()) {
			return visibility;
		}

		visibility.showHierarchy = data.value("hierarchy", visibility.showHierarchy);
		visibility.showInspector = data.value("inspector", visibility.showInspector);
		visibility.showProject = data.value("project", visibility.showProject);
		visibility.showConsole = data.value("console", visibility.showConsole);
		visibility.showSceneView = data.value("sceneView", visibility.showSceneView);
		visibility.showGameView = data.value("gameView", visibility.showGameView);
		visibility.showToolbar = data.value("toolbar", visibility.showToolbar);
		visibility.showTool = data.value("tool", visibility.showTool);
		return visibility;
	}

	nlohmann::json MakeLayoutJson(const Engine::EditorLayoutSnapshot& layout, bool imported) {

		nlohmann::json panels = nlohmann::json::array();
		for (const Engine::EditorPanelLayoutSnapshot& panel : layout.panels) {

			panels.push_back({
				{ "typeID", panel.typeID },
				{ "instanceID", panel.instanceID },
				{ "primary", panel.primary },
				{ "open", panel.open },
				{ "state", panel.state },
				});
		}

		return {
			{ "layoutID", layout.layoutID },
			{ "displayName", layout.displayName },
			{ "order", layout.order },
			{ "builtinDefault", layout.builtinDefault },
			{ "imported", imported },
			{ "visibility", MakeVisibilityJson(layout.visibility) },
			{ "panels", panels },
			{ "imguiIniData", layout.imguiIniData },
		};
	}

	bool ReadLayout(const nlohmann::json& data, Engine::EditorLayoutSnapshot& outLayout, bool& outImported) {

		if (!data.is_object()) {
			return false;
		}

		outLayout.layoutID = data.value("layoutID", std::string{});
		outLayout.displayName = data.value("displayName", std::string{});
		if (outLayout.layoutID.empty() || outLayout.displayName.empty()) {
			return false;
		}

		outLayout.order = data.value("order", 0);
		outLayout.builtinDefault = data.value("builtinDefault", false);
		outImported = data.value("imported", false);
		outLayout.visibility = ReadVisibility(data.value("visibility", nlohmann::json::object()));
		outLayout.imguiIniData = data.value("imguiIniData", std::string{});
		outLayout.panels.clear();

		const nlohmann::json panels = data.value("panels", nlohmann::json::array());
		if (panels.is_array()) {
			for (const nlohmann::json& panelData : panels) {

				if (!panelData.is_object()) {
					continue;
				}

				Engine::EditorPanelLayoutSnapshot panel{};
				panel.typeID = panelData.value("typeID", std::string{});
				panel.instanceID = panelData.value("instanceID", std::string{});
				if (panel.typeID.empty() || panel.instanceID.empty()) {
					continue;
				}
				panel.primary = panelData.value("primary", false);
				panel.open = panelData.value("open", true);
				panel.state = panelData.value("state", nlohmann::json::object());
				outLayout.panels.emplace_back(std::move(panel));
			}
		}
		return true;
	}

	std::string MakeComparableName(const std::string& name) {

		std::string result;
		result.reserve(name.size());
		for (const unsigned char character : name) {
			result.push_back(static_cast<char>(std::tolower(character)));
		}
		return result;
	}
}

void Engine::EditorLayoutManager::Init() {

	engineCatalogPath_ = RuntimePaths::GetEngineAssetPath("Config/editorLayouts.exeConfig.json");
	userCatalogPath_ = RuntimePaths::GetUserSettingsPath(ConfigPaths::kEditorLayouts);
	sessionPath_ = RuntimePaths::GetUserSettingsPath(ConfigPaths::kEditorLayoutSession);

	LoadCatalog(engineCatalogPath_, false, engineLayouts_, &engineDefaultLayoutID_);
	if (engineLayouts_.empty()) {
		engineLayouts_.push_back({ MakeBuiltinDefaultLayout(), false });
		engineDefaultLayoutID_ = engineLayouts_.front().layout.layoutID;
	}
	LoadCatalog(userCatalogPath_, false, userLayouts_);
	RebuildMenuEntries();
}

bool Engine::EditorLayoutManager::LoadStartupLayout(EditorLayoutSnapshot& outLayout) {

	if (JsonAdapter::Check(sessionPath_.string(), false)) {

		const nlohmann::json data = JsonAdapter::Load(sessionPath_.string(), false);
		if (data.is_object() && data.value("schemaVersion", 0) == kSchemaVersion &&
			data.contains("layout")) {

			bool imported = false;
			if (ReadLayout(data["layout"], outLayout, imported)) {
				activeLayoutID_ = data.value("activeLayoutID", outLayout.layoutID);
				return true;
			}
		}
	}

	const EditorLayoutSnapshot* defaultLayout = FindLayout(engineDefaultLayoutID_);
	if (!defaultLayout) {
		outLayout = MakeBuiltinDefaultLayout();
		activeLayoutID_ = outLayout.layoutID;
		return true;
	}

	outLayout = *defaultLayout;
	activeLayoutID_ = defaultLayout->layoutID;
	return true;
}

bool Engine::EditorLayoutManager::SaveUserLayout(const std::string& name,
	const EditorLayoutSnapshot& source, std::string& outLayoutID, std::string& outError) {

	if (!ValidateSaveName(name, outError)) {
		return false;
	}

	const std::string comparableName = MakeComparableName(name);
	auto found = std::find_if(userLayouts_.begin(), userLayouts_.end(), [&](const StoredLayout& stored) {
		return MakeComparableName(stored.layout.displayName) == comparableName;
		});
	if (found != userLayouts_.end()) {

		const std::string layoutID = found->layout.layoutID;
		const int32_t order = found->layout.order;
		const bool imported = found->imported;
		found->layout = source;
		found->layout.layoutID = layoutID;
		found->layout.displayName = name;
		found->layout.order = order;
		found->layout.builtinDefault = layoutID == engineDefaultLayoutID_;
		found->imported = imported;
		outLayoutID = layoutID;
	} else {

		const EditorLayoutSnapshot* defaultLayout = FindLayout(engineDefaultLayoutID_);
		if (defaultLayout && MakeComparableName(defaultLayout->displayName) == comparableName) {

			StoredLayout stored{};
			stored.layout = source;
			stored.layout.layoutID = engineDefaultLayoutID_;
			stored.layout.displayName = defaultLayout->displayName;
			stored.layout.order = 0;
			stored.layout.builtinDefault = true;
			stored.imported = false;
			userLayouts_.emplace_back(std::move(stored));
			outLayoutID = engineDefaultLayoutID_;
		} else {

			StoredLayout stored{};
			stored.layout = source;
			stored.layout.layoutID = "user." + ToString(UUID::New());
			stored.layout.displayName = name;
			stored.layout.order = GetNextUserOrder();
			stored.layout.builtinDefault = false;
			stored.imported = false;
			userLayouts_.emplace_back(std::move(stored));
			outLayoutID = userLayouts_.back().layout.layoutID;
		}
	}

	activeLayoutID_ = outLayoutID;
	SaveCatalog(userCatalogPath_, userLayouts_);
	RebuildMenuEntries();
	return true;
}

bool Engine::EditorLayoutManager::SaveAllEngineLayouts(std::string& outError) {

	if (!IsEngineSourceProject()) {
		outError = "エンジンソース環境でのみ保存できます";
		return false;
	}

	const EditorLayoutSnapshot* defaultLayout = FindLayout(engineDefaultLayoutID_);
	if (!defaultLayout) {
		outError = "Defaultレイアウトが見つかりません";
		return false;
	}

	std::vector<StoredLayout> exportedLayouts;
	exportedLayouts.reserve(userLayouts_.size() + 1);
	StoredLayout exportedDefault{};
	exportedDefault.layout = *defaultLayout;
	exportedDefault.layout.layoutID = engineDefaultLayoutID_;
	exportedDefault.layout.order = 0;
	exportedDefault.layout.builtinDefault = true;
	exportedLayouts.emplace_back(std::move(exportedDefault));

	std::vector<const StoredLayout*> sortedLayouts;
	sortedLayouts.reserve(userLayouts_.size());
	for (const StoredLayout& stored : userLayouts_) {
		if (stored.layout.layoutID != engineDefaultLayoutID_) {
			sortedLayouts.emplace_back(&stored);
		}
	}
	std::stable_sort(sortedLayouts.begin(), sortedLayouts.end(), [](const StoredLayout* lhs, const StoredLayout* rhs) {
		return lhs->layout.order < rhs->layout.order;
		});

	int32_t order = 1;
	for (const StoredLayout* source : sortedLayouts) {

		StoredLayout exported{};
		exported.layout = source->layout;
		exported.layout.order = order++;
		exported.layout.builtinDefault = false;
		exported.imported = false;

		const bool engineLayoutID = exported.layout.layoutID.rfind("engine.", 0) == 0;
		if (!engineLayoutID) {

			const std::string comparableName = MakeComparableName(exported.layout.displayName);
			const auto found = std::find_if(engineLayouts_.begin(), engineLayouts_.end(), [&](const StoredLayout& stored) {
				return stored.layout.layoutID != engineDefaultLayoutID_ &&
					MakeComparableName(stored.layout.displayName) == comparableName;
				});
			exported.layout.layoutID = found != engineLayouts_.end() ?
				found->layout.layoutID : "engine." + ToString(UUID::New());
		}
		exportedLayouts.emplace_back(std::move(exported));
	}

	engineLayouts_ = std::move(exportedLayouts);
	SaveCatalog(engineCatalogPath_, engineLayouts_, &engineDefaultLayoutID_);
	RebuildMenuEntries();
	return true;
}

bool Engine::EditorLayoutManager::ImportEngineLayouts(EditorLayoutSnapshot& outDefaultLayout,
	std::string& outError) {

	std::vector<StoredLayout> loadedLayouts;
	std::string loadedDefaultLayoutID = engineDefaultLayoutID_;
	LoadCatalog(engineCatalogPath_, false, loadedLayouts, &loadedDefaultLayoutID);
	if (loadedLayouts.empty()) {
		outError = "エンジンレイアウトが見つかりません";
		return false;
	}

	const auto defaultFound = std::find_if(loadedLayouts.begin(), loadedLayouts.end(), [&](const StoredLayout& stored) {
		return stored.layout.layoutID == loadedDefaultLayoutID;
		});
	const EditorLayoutSnapshot* defaultLayout = defaultFound != loadedLayouts.end() ? &defaultFound->layout : nullptr;
	if (!defaultLayout) {
		outError = "Defaultレイアウトが見つかりません";
		return false;
	}

	outDefaultLayout = *defaultLayout;
	engineLayouts_ = std::move(loadedLayouts);
	engineDefaultLayoutID_ = loadedDefaultLayoutID;

	std::unordered_set<std::string> engineLayoutIDs;
	engineLayoutIDs.reserve(engineLayouts_.size());
	for (const StoredLayout& engineLayout : engineLayouts_) {
		engineLayoutIDs.emplace(engineLayout.layout.layoutID);
	}
	userLayouts_.erase(std::remove_if(userLayouts_.begin(), userLayouts_.end(), [&](const StoredLayout& stored) {
		return stored.imported && !engineLayoutIDs.contains(stored.layout.layoutID);
		}), userLayouts_.end());

	for (const StoredLayout& engineLayout : engineLayouts_) {

		auto found = std::find_if(userLayouts_.begin(), userLayouts_.end(), [&](const StoredLayout& userLayout) {
			return userLayout.layout.layoutID == engineLayout.layout.layoutID;
			});
		if (found != userLayouts_.end()) {

			const int32_t order = found->layout.order;
			found->layout = engineLayout.layout;
			found->layout.order = order;
			found->imported = true;
		} else {

			StoredLayout imported = engineLayout;
			imported.layout.order = imported.layout.layoutID == engineDefaultLayoutID_ ? 0 : GetNextUserOrder();
			imported.imported = true;
			userLayouts_.emplace_back(std::move(imported));
		}
	}

	activeLayoutID_ = engineDefaultLayoutID_;
	SaveCatalog(userCatalogPath_, userLayouts_);
	RebuildMenuEntries();
	return true;
}

bool Engine::EditorLayoutManager::DeleteLayout(const std::string& layoutID) {

	if (layoutID == engineDefaultLayoutID_) {
		return false;
	}

	const auto found = std::find_if(userLayouts_.begin(), userLayouts_.end(), [&](const StoredLayout& stored) {
		return stored.layout.layoutID == layoutID;
		});
	if (found == userLayouts_.end()) {
		return false;
	}

	userLayouts_.erase(found);
	if (activeLayoutID_ == layoutID) {
		activeLayoutID_ = engineDefaultLayoutID_;
	}
	SaveCatalog(userCatalogPath_, userLayouts_);
	RebuildMenuEntries();
	return true;
}

void Engine::EditorLayoutManager::SaveSession(const EditorLayoutSnapshot& layout) const {

	nlohmann::json data = nlohmann::json::object();
	data["schemaVersion"] = kSchemaVersion;
	data["activeLayoutID"] = activeLayoutID_;
	data["layout"] = MakeLayoutJson(layout, false);
	JsonAdapter::Save(sessionPath_.string(), data);
}

const Engine::EditorLayoutSnapshot* Engine::EditorLayoutManager::FindLayout(const std::string& layoutID) const {

	const auto find = [&](const std::vector<StoredLayout>& layouts) -> const EditorLayoutSnapshot* {
		const auto found = std::find_if(layouts.begin(), layouts.end(), [&](const StoredLayout& stored) {
			return stored.layout.layoutID == layoutID;
			});
		return found != layouts.end() ? &found->layout : nullptr;
		};

	if (const EditorLayoutSnapshot* userLayout = find(userLayouts_)) {
		return userLayout;
	}
	return find(engineLayouts_);
}

bool Engine::EditorLayoutManager::IsEngineSourceProject() const {

	std::error_code ec;
	return std::filesystem::equivalent(
		RuntimePaths::GetProjectRoot(), RuntimePaths::GetEngineProjectRoot(), ec) && !ec;
}

void Engine::EditorLayoutManager::LoadCatalog(const std::filesystem::path& path, bool imported,
	std::vector<StoredLayout>& outLayouts, std::string* outDefaultLayoutID) {

	outLayouts.clear();
	if (!JsonAdapter::Check(path.string(), false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	if (!data.is_object() || data.value("schemaVersion", 0) != kSchemaVersion) {
		return;
	}
	if (outDefaultLayoutID) {
		*outDefaultLayoutID = data.value("defaultLayoutID", *outDefaultLayoutID);
	}

	const nlohmann::json layouts = data.value("layouts", nlohmann::json::array());
	if (!layouts.is_array()) {
		return;
	}
	for (const nlohmann::json& layoutData : layouts) {

		StoredLayout stored{};
		stored.imported = imported;
		if (ReadLayout(layoutData, stored.layout, stored.imported)) {
			outLayouts.emplace_back(std::move(stored));
		}
	}
}

void Engine::EditorLayoutManager::SaveCatalog(const std::filesystem::path& path,
	const std::vector<StoredLayout>& layouts, const std::string* defaultLayoutID) const {

	nlohmann::json data = nlohmann::json::object();
	data["schemaVersion"] = kSchemaVersion;
	if (defaultLayoutID) {
		data["defaultLayoutID"] = *defaultLayoutID;
	}
	data["layouts"] = nlohmann::json::array();
	for (const StoredLayout& stored : layouts) {
		data["layouts"].push_back(MakeLayoutJson(stored.layout, stored.imported));
	}
	JsonAdapter::Save(path.string(), data);
}

void Engine::EditorLayoutManager::RebuildMenuEntries() {

	menuEntries_.clear();
	if (const EditorLayoutSnapshot* defaultLayout = FindLayout(engineDefaultLayoutID_)) {
		menuEntries_.push_back({
			.layoutID = defaultLayout->layoutID,
			.displayName = defaultLayout->displayName,
			.defaultLayout = true,
			.imported = false,
			});
	}

	std::vector<const StoredLayout*> sorted;
	sorted.reserve(userLayouts_.size());
	for (const StoredLayout& layout : userLayouts_) {
		if (layout.layout.layoutID != engineDefaultLayoutID_) {
			sorted.push_back(&layout);
		}
	}
	std::stable_sort(sorted.begin(), sorted.end(), [](const StoredLayout* lhs, const StoredLayout* rhs) {
		return lhs->layout.order < rhs->layout.order;
		});

	for (const StoredLayout* stored : sorted) {
		menuEntries_.push_back({
			.layoutID = stored->layout.layoutID,
			.displayName = stored->layout.displayName,
			.defaultLayout = false,
			.imported = stored->imported,
			});
	}
}

bool Engine::EditorLayoutManager::ValidateSaveName(const std::string& name, std::string& outError) const {

	if (name.empty()) {
		outError = "レイアウト名を入力してください";
		return false;
	}
	if (64 < name.size()) {
		outError = "レイアウト名は64文字以内にしてください";
		return false;
	}

	return true;
}

Engine::EditorLayoutSnapshot Engine::EditorLayoutManager::MakeBuiltinDefaultLayout() const {

	EditorLayoutSnapshot layout{};
	layout.layoutID = "engine.default";
	layout.displayName = "Default";
	layout.builtinDefault = true;
	layout.panels = {
		{ "Project", "project.primary", true, true, nlohmann::json::object() },
		{ "Inspector", "inspector.primary", true, true, { { "mode", "FollowSelection" } } },
	};
	return layout;
}

int32_t Engine::EditorLayoutManager::GetNextUserOrder() const {

	int32_t order = 0;
	for (const StoredLayout& layout : userLayouts_) {
		order = (std::max)(order, layout.layout.order + 1);
	}
	return order;
}
