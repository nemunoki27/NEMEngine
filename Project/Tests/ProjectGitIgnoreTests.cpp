#include "ProjectGitIgnoreTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectGitIgnoreDocument.h>
#include <Engine/Editor/Assets/Project/ProjectGitIgnoreService.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <string>
#include <optional>
#include <thread>
#include <iostream>

bool NEMTests::TestProjectGitIgnoreDocument() {

	using Engine::ProjectGitIgnoreDocument::Update;
	const std::string original = "# user rules\r\n*.tmp\r\n!keep.tmp\r\n";
	std::string excluded, included, repeated, error;
	// 既存規則と改行を残し、本体とmetaを除外する
	if (!Update(original, "Project/GameAssets/My Model[1].fbx", false, false, excluded, error) ||
		excluded.find(original) != 0 ||
		excluded.find("/Project/GameAssets/My\\ Model\\[1\\].fbx\r\n") == std::string::npos ||
		excluded.find("/Project/GameAssets/My\\ Model\\[1\\].fbx.meta\r\n") == std::string::npos) {
		return false;
	}
	// 同じ操作を繰り返しても規則を重複させない
	if (!Update(excluded, "Project/GameAssets/My Model[1].fbx", false, false, repeated, error) || repeated != excluded) {
		return false;
	}
	if (!Update(excluded, "Project/GameAssets/My Model[1].fbx", false, true, included, error) ||
		included.find("!/Project/GameAssets/My\\ Model\\[1\\].fbx\r\n") == std::string::npos ||
		included.find(original) != 0) { return false; }
	// 親を含め直しても子の個別除外は保持する
	if (!Update(included, "Project/GameAssets/Models/child.fbx", false, false, excluded, error) ||
		!Update(excluded, "Project/GameAssets/Models", true, false, repeated, error) ||
		repeated.find("/Project/GameAssets/Models/\r\n") == std::string::npos ||
		!Update(repeated, "Project/GameAssets/Models", true, true, included, error) ||
		included.find("!/Project/GameAssets/Models/\r\n") == std::string::npos ||
		included.find("/Project/GameAssets/Models/child.fbx\r\n") == std::string::npos) { return false; }
	// 設定区間より後の手書き規則も変更しない
	const std::string suffix = "# trailing rule\n*.cache\n";
	if (!Update(included + suffix, "Project/GameAssets/日本語.png", false, false, repeated, error) ||
		!repeated.ends_with(suffix)) { return false; }
	// 壊れた設定区間や外部パスを拒否する
	for (const auto& content : { "# NEMEngine Project exclusions begin\n", "# NEMEngine Project exclusions end\n" }) {
		if (Update(content, "a.png", false, false, repeated, error) || error.empty()) { return false; }
	}
	const std::string duplicate = "# NEMEngine Project exclusions begin\n# NEMEngine Project exclusions end\n";
	if (Update(duplicate + duplicate, "a.png", false, false, repeated, error)) { return false; }
	for (const auto& path : { "../a", "/a", "a/../b", ".git/config", "a\nb", "a//b" }) {
		if (Update(original, path, false, false, repeated, error)) { return false; }
	}
	return true;
}

bool NEMTests::TestProjectGitIgnoreReadOnly() {

	using namespace Engine;
	ProjectGitIgnoreService service;
	auto query = [&](const std::filesystem::path& path) -> std::optional<ProjectGitIgnoreState> {
		service.Request(path, true);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (service.IsBusy()) {
			service.Poll();
			if (std::chrono::steady_clock::now() >= deadline) { return std::nullopt; }
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		const auto* state = service.Request(path, true);
		if (state && !state->valid) { std::cerr << "Git status failed: " << state->message << '\n'; }
		return state ? std::optional<ProjectGitIgnoreState>(*state) : std::nullopt;
	};
	// 実Repositoryの状態だけを取得し、設定や索引を変更しない
	const auto assets = query(RuntimePaths::ResolveAssetPath("Engine/Assets"));
	if (!assets || !assets->valid || assets->parentExcluded || assets->relativePath.find("..") != std::string::npos) {
		return false;
	}
	const auto ignored = query(assets->repository / "Generated" / "Refactoring");
	if (!ignored || !ignored->valid || !ignored->excluded || !ignored->parentExcluded) { return false; }
	// パス表記の大小文字が違ってもRepository内として解決する
	const auto lowerPath = Algorithm::PathFromUTF8(Algorithm::ToLower(Algorithm::PathToUTF8(assets->target)));
	const auto lower = query(lowerPath);
	return lower && lower->valid && lower->relativePath.find("..") == std::string::npos;
}
