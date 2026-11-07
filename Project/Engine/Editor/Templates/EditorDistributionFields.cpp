#include "EditorDistributionSession.h"

//============================================================================
//	include
//============================================================================
#include "EditorTemplateStorage.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace {

	// 一覧の選択と詳細を分けて表示する
	bool BeginDetails(const char* id) {

		ImGui::SameLine();
		return ImGui::BeginChild(id, ImVec2(0, -180), ImGuiChildFlags_None);
	}

	// JSON上の文字列を共通GUIで編集する
	void StringField(const char* label, nlohmann::json& data, const char* key, bool multiLine = false) {

		std::string value = data.value(key, std::string{});
		Engine::TextEditSetting setting;
		setting.multiLine = multiLine;
		setting.size = ImVec2(0, 80);
		if (Engine::MyGUI::InputText(label, value, setting).valueChanged) data[key] = value;
	}

	// 最後に取得した配布一覧と現在の設定を照合する
	std::string PublishStatus(const char* category, const std::string& id, const std::string& version,
		const std::string& engineVersion = {}) try {

		nlohmann::json catalog;
		if (!Engine::JsonFile::TryLoad(Engine::EditorTemplateStorage::Repository() / "Generated/Distribution/catalog.json", catalog) ||
			!catalog.contains(category) || !catalog[category].is_array()) return "下書き";
		std::string previous;
		for (const auto& release : catalog[category]) {
			if (release.value("id", std::string{}) != id) continue;
			const auto published = release.value("version", std::string{});
			if (published == version && (engineVersion.empty() || release.value("engineVersion", std::string{}) == engineVersion))
				return "公開済み " + published;
			previous = published;
		}
		return previous.empty() ? "下書き" : "下書き / 公開版 " + previous;
	} catch (const nlohmann::json::exception&) {
		return "公開状態を取得できません";
	}
}

void Engine::EditorDistributionSession::DrawSamples() {

	ImGui::BeginChild("SampleList", ImVec2(250, -180), ImGuiChildFlags_Borders);
	for (const auto& entry : samples_) {
		if (ImGui::Selectable((entry.name + "##" + entry.id).c_str(), sample_.id == entry.id)) {
			sample_ = entry;
			message_.clear();
		}
	}
	if (samples_.empty()) ImGui::TextWrapped("テンプレートメニューからサンプルを保存してください");
	ImGui::EndChild();
	if (BeginDetails("SampleDetails")) {
		if (!sample_.id.empty()) {
			MyGUI::ScopedPropertyLabelWidth labels("SamplePublishFields");
			MyGUI::Text("名前", sample_.name);
			MyGUI::Text("説明", sample_.description);
			MyGUI::Text("公開状態", PublishStatus("samples", sample_.id, sample_.version, sample_.engineVersion));
			MyGUI::InputText("版番号", sample_.version);
			MyGUI::InputText("対応エンジン版", sample_.engineVersion);
			MyGUI::Text("保存元", Algorithm::PathToUTF8(sample_.root));
			ImGui::Separator();
			const float width = MyGUI::CalcEqualItemWidth(2);
			if (ImGui::Button("設定を保存", ImVec2(width, 0))) {
				failed_ = !EditorTemplateStorage::WriteEntry(sample_, sample_.root);
				message_ = failed_ ? "設定を保存できません" : "設定を保存しました";
			}
			ImGui::SameLine();
			if (ImGui::Button("読み込み", ImVec2(width, 0))) {
				const auto result = EditorTemplateStorage::OpenWorkspace(sample_);
				message_ = result.message;
				failed_ = !result.success;
			}
			if (ImGui::Button("公開", ImVec2(width, 0))) {
				if (EditorTemplateStorage::WriteEntry(sample_, sample_.root)) Start({
					{"action", "publish"}, {"kind", "sample"}, {"id", sample_.id}, {"name", sample_.name},
					{"description", sample_.description}, {"version", sample_.version}, {"engineVersion", sample_.engineVersion},
					{"sourceDirectory", Algorithm::PathToUTF8(sample_.root)}});
			}
			ImGui::SameLine();
			if (ImGui::Button("公開を停止", ImVec2(width, 0))) Start({{"action", "withdraw"}, {"kind", "sample"}, {"id", sample_.id}});
			if (ImGui::Button("削除", ImVec2(ImGui::GetContentRegionAvail().x, 0))) RequestDeleteSample(sample_);
		}
	}
	ImGui::EndChild();
}

void Engine::EditorDistributionSession::DrawAssets() {

	ImGui::BeginChild("AssetList", ImVec2(250, -180), ImGuiChildFlags_Borders);
	if (ImGui::Button("アセットを追加", ImVec2(ImGui::GetContentRegionAvail().x, 0))) { NewAsset(); message_.clear(); }
	ImGui::Separator();
	for (const auto& entry : assets_) {
		const auto id = entry.value("id", std::string{});
		if (ImGui::Selectable((entry.value("name", std::string{}) + "##" + id).c_str(), asset_.value("id", std::string{}) == id)) {
			asset_ = entry;
			message_.clear();
		}
	}
	ImGui::EndChild();
	if (BeginDetails("AssetDetails")) {
		MyGUI::ScopedPropertyLabelWidth labels("AssetPublishFields");
		StringField("名前", asset_, "name");
		MyGUI::Text("公開状態", PublishStatus("assets", asset_.value("id", std::string{}), asset_.value("version", std::string{})));
		StringField("説明", asset_, "description", true);
		StringField("版番号", asset_, "version");
		StringField("推奨エンジン版", asset_, "recommendedEngineVersion");
		MyGUI::Text("配布ZIP", asset_.value("archivePath", std::string{}));
		if (ImGui::Button("ZIPを選択", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
			archiveDialog_.OpenFile(RuntimePaths::GetProjectRoot(), L"*.zip");
		ImGui::Separator();
		const float width = MyGUI::CalcEqualItemWidth(2);
		if (ImGui::Button("保存", ImVec2(width, 0))) SaveAsset();
		ImGui::SameLine();
		if (ImGui::Button("公開", ImVec2(width, 0)) && SaveAsset()) {
			auto request = asset_;
			request["action"] = "publish";
			request["kind"] = "asset";
			Start(std::move(request));
		}
		if (ImGui::Button("公開を停止", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
			Start({{"action", "withdraw"}, {"kind", "asset"}, {"id", asset_.value("id", std::string{})}});
	}
	ImGui::EndChild();
}
