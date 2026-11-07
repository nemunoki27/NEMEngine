#include "EditorDistributionSession.h"

//============================================================================
//	include
//============================================================================
#include "EditorTemplateStorage.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <regex>
#include <stdexcept>

namespace {

	// 外部プロセスへパスを一引数として渡す
	std::wstring QuotePath(const std::filesystem::path& path) {

		return L"\"" + path.wstring() + L"\"";
	}

	// 配布元の版番号を検証する
	bool IsVersion(const std::string& value) {

		static const std::regex version(R"((0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))");
		return std::regex_match(value, version);
	}

	// 配布先の既定エンジン版を取得する
	std::string LatestVersion() {

		nlohmann::json catalog;
		if (!Engine::JsonFile::TryLoad(Engine::EditorTemplateStorage::Repository() / "Generated/Distribution/catalog.json", catalog)) return {};
		if (!catalog.contains("engines") || !catalog["engines"].is_array() || catalog["engines"].empty()) return {};
		return catalog["engines"].back().value("version", std::string{});
	}
}

void Engine::EditorDistributionSession::Open() {

	if (!EditorSourceNavigation::IsEngineSourceProject()) return;
	open_ = true;
	Refresh();
	if (asset_.is_null()) NewAsset();
}

void Engine::EditorDistributionSession::Refresh() {

	samples_ = EditorTemplateStorage::ListSamples();
	assets_.clear();
	std::error_code error;
	const auto root = EditorTemplateStorage::Repository() / "Templates/Assets";
	for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
		if (!it->is_directory(error) || it->is_symlink(error)) continue;
		nlohmann::json data;
		try {
			if (JsonFile::TryLoad(it->path() / "asset.json", data) && data.is_object() &&
				TryParseAssetGUID32Hex(data.value("id", std::string{}))) {
				const char* fields[]{"id", "name", "description", "version", "archivePath", "recommendedEngineVersion", "fileName"};
				if (!std::all_of(std::begin(fields), std::end(fields), [&](const char* key) {
					return data.contains(key) && data[key].is_string();
				})) continue;
				const auto archive = Algorithm::PathFromUTF8(data.value("archivePath", std::string{}));
				data["archivePath"] = Algorithm::PathToUTF8(archive.is_absolute() ? archive : it->path() / archive);
				assets_.push_back(std::move(data));
			}
		} catch (const nlohmann::json::exception&) { }
	}
	std::sort(assets_.begin(), assets_.end(), [](const auto& a, const auto& b) {
		return a.value("name", std::string{}) < b.value("name", std::string{});
	});
	if (sample_.id.empty() && !samples_.empty()) sample_ = samples_.front();
	if (sample_.engineVersion.empty()) sample_.engineVersion = LatestVersion();
}

void Engine::EditorDistributionSession::NewAsset() {

	asset_ = {{"schemaVersion", 1}, {"id", ToString(AssetID::New())}, {"name", ""}, {"description", ""},
		{"version", "1.0.0"}, {"recommendedEngineVersion", LatestVersion()}, {"archivePath", ""}, {"fileName", ""}};
}

bool Engine::EditorDistributionSession::SaveAsset() {

	try {
		const auto id = asset_.value("id", std::string{});
		if (!TryParseAssetGUID32Hex(id) || asset_.value("name", std::string{}).empty() || !IsVersion(asset_.value("version", std::string{})))
			throw std::runtime_error("名前と版番号を入力してください");
		const auto archive = Algorithm::PathFromUTF8(asset_.value("archivePath", std::string{}));
		if (Algorithm::ToLower(archive.extension().string()) != ".zip" || !std::filesystem::is_regular_file(archive))
			throw std::runtime_error("配布するZIPファイルを選択してください");
		const auto root = EditorTemplateStorage::Repository() / "Templates/Assets" / id;
		if (!StorageFileUtility::IsInside(root, EditorTemplateStorage::Repository()))
			throw std::runtime_error("アセット配布元はエンジンリポジトリ内へ保存してください");
		std::filesystem::create_directories(root);
		const auto target = root / archive.filename();
		if (std::filesystem::weakly_canonical(archive) != std::filesystem::weakly_canonical(target)) {
			if (!StorageFileUtility::CopyFileAtomically(archive, target)) throw std::runtime_error("配布ZIPを保存できません");
		}
		asset_["archivePath"] = Algorithm::PathToUTF8(target);
		asset_["fileName"] = Algorithm::PathToUTF8(target.filename());
		auto stored = asset_;
		stored["archivePath"] = Algorithm::PathToUTF8(target.filename());
		if (!JsonFile::Save(root / "asset.json", stored)) throw std::runtime_error("アセットの配布設定を保存できません");
		message_ = "アセットの配布元を保存しました";
		failed_ = false;
		Refresh();
		return true;
	} catch (const std::exception& error) { message_ = error.what(); failed_ = true; return false; }
}

void Engine::EditorDistributionSession::Start(nlohmann::json request, bool removeSample) {

	if (process_.IsRunning() || !EditorSourceNavigation::IsEngineSourceProject()) return;
	try {
		request["schemaVersion"] = 1;
		request["baseUrl"] = "";
		const auto repository = EditorTemplateStorage::Repository();
		const auto directory = repository / "Generated/DistributionContent/Jobs" / ToString(AssetID::New());
		std::filesystem::create_directories(directory);
		requestPath_ = directory / "request.json";
		if (!JsonFile::Save(requestPath_, request)) throw std::runtime_error("公開要求を保存できません");
		operation_ = std::move(request);
		deleteAfterWithdraw_ = removeSample;
		output_.clear();
		failed_ = false;
		if (!process_.Start(L"node " + QuotePath(repository / "Tools/Distribution/content.mjs") +
			L" --request " + QuotePath(requestPath_), repository)) throw std::runtime_error("配布処理を開始できません Node.jsの実行環境を確認してください");
		message_ = "配布処理を実行しています";
	} catch (const std::exception& error) {
		message_ = error.what();
		failed_ = true;
		if (!process_.IsRunning() && !requestPath_.empty()) {
			std::error_code cleanup;
			std::filesystem::remove(requestPath_, cleanup);
			std::filesystem::remove(requestPath_.parent_path(), cleanup);
		}
	}
}

void Engine::EditorDistributionSession::Update() {

	std::optional<std::filesystem::path> selected;
	if (archiveDialog_.Poll(selected) && selected) asset_["archivePath"] = Algorithm::PathToUTF8(*selected);
	if (!process_.IsRunning()) return;
	const bool completed = process_.Poll([this](const std::string& line) {
		message_ = line;
		if (output_.size() > 16000) output_.erase(0, 8000);
		output_ += line + "\n";
	});
	if (!completed) return;
	failed_ = process_.ExitCode() != 0;
	if (!failed_ && deleteAfterWithdraw_) {
		const auto result = EditorTemplateStorage::RemoveSample(operation_.value("id", std::string{}));
		message_ = result.message;
		failed_ = !result.success;
		if (result.success && sample_.id == operation_.value("id", std::string{})) sample_ = {};
	} else if (!failed_) message_ = operation_.value("action", std::string{}) == "withdraw" ? "公開を停止しました" : "公開しました";
	else if (message_.empty()) message_ = "配布処理に失敗しました";
	deleteAfterWithdraw_ = false;
	std::error_code error;
	std::filesystem::remove(std::filesystem::path(requestPath_.native() + L".cancel"), error);
	std::filesystem::remove(requestPath_, error);
	std::filesystem::remove(requestPath_.parent_path(), error);
	Refresh();
}

void Engine::EditorDistributionSession::RequestDeleteSample(const EditorTemplateEntry& entry) {

	deleteSample_ = entry;
	confirmDelete_ = true;
}

void Engine::EditorDistributionSession::DrawDeletePopup() {

	if (confirmDelete_) { ImGui::OpenPopup("サンプルプロジェクトを削除"); confirmDelete_ = false; }
	if (!MyGUI::BeginPopupModal("サンプルプロジェクトを削除", nullptr, ImGuiWindowFlags_AlwaysAutoResize, 620)) return;
	ImGui::TextWrapped("「%s」を配布一覧から取り下げ、保存元を削除します。作成済みプロジェクトには影響しません。", deleteSample_.name.c_str());
	const float width = MyGUI::CalcEqualItemWidth(2);
	if (ImGui::Button("削除", ImVec2(width, 0))) {
		Start({{"action", "withdraw"}, {"kind", "sample"}, {"id", deleteSample_.id}}, true);
		open_ = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(width, 0))) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void Engine::EditorDistributionSession::Draw() {

	Update();
	if (open_) {
		ImGui::SetNextWindowSize(ImVec2(950, 640), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("配布管理", &open_)) {
			ImGui::BeginDisabled(IsBusy());
			if (ImGui::BeginTabBar("DistributionCategories")) {
				if (ImGui::BeginTabItem("サンプルプロジェクト")) { DrawSamples(); ImGui::EndTabItem(); }
				if (ImGui::BeginTabItem("アセット")) { DrawAssets(); ImGui::EndTabItem(); }
				ImGui::EndTabBar();
			}
			ImGui::EndDisabled();
			ImGui::Separator();
			if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
			if (IsBusy()) {
				const auto marker = std::filesystem::path(requestPath_.native() + L".cancel");
				ImGui::BeginDisabled(std::filesystem::exists(marker));
				if (ImGui::Button("キャンセル")) {
					if (JsonFile::Save(marker, {{"cancel", true}})) message_ = "操作のキャンセルを要求しました";
					else message_ = "キャンセル要求を保存できません";
				}
				ImGui::EndDisabled();
			}
			if (!output_.empty() && MyGUI::CollapsingHeader("実行ログ", false)) {
				ImGui::BeginChild("DistributionOutput", ImVec2(0, 130));
				ImGui::TextUnformatted(output_.c_str());
				ImGui::EndChild();
			}
		}
		ImGui::End();
	}
	DrawDeletePopup();
}
