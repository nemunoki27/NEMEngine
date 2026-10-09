#include "ProjectGitIgnoreService.h"

//============================================================================
//	include
//============================================================================
#include "ProjectGitIgnoreDocument.h"
#include <Engine/Core/Scripting/Managed/ManagedProcessRunner.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

	struct GitResult {

		int32_t code = -1;
		std::string output;
	};

	// shellを経由せず、読み取り専用のGitコマンドを実行する
	GitResult RunGit(const std::filesystem::path& directory, const std::vector<std::string>& arguments) {

		std::wstring command = L"git --no-optional-locks -c core.quotepath=false";
		for (const auto& argument : arguments) {
			// パスは区切りを統一し、引用符と制御文字を拒否する
			if (argument.find_first_of("\"\r\n\0", 0, 4) != std::string::npos) {
				throw std::runtime_error("Gitの引数が不正です");
			}
			command += L" \"" + Engine::Algorithm::ConvertStringStrict(argument) + L"\"";
		}
		Engine::ManagedProcessRunner process;
		if (!process.Start(command, directory)) { throw std::runtime_error("Gitを起動できません"); }
		GitResult result;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!process.Poll([&](const std::string& line) {
			if (result.output.size() < 4096) { result.output += line + "\n"; }
		})) {
			if (std::chrono::steady_clock::now() >= deadline) {
				process.Terminate();
				throw std::runtime_error("Gitの状態取得が時間切れになりました");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		result.code = process.ExitCode();
		return result;
	}

	// 追跡済みのファイルも除外設定として判定する
	bool IsIgnored(const std::filesystem::path& root, const std::string& path) {

		const auto result = RunGit(root, { "check-ignore", "--no-index", "--quiet", "--", path });
		if (result.code != 0 && result.code != 1) {
			throw std::runtime_error("Gitの除外状態を取得できません: " + result.output);
		}
		return result.code == 0;
	}

	// 読み取り失敗を空ファイルとして扱わない
	std::string ReadDocument(const std::filesystem::path& path) {

		if (!std::filesystem::exists(path)) { return {}; }
		std::ifstream input(path, std::ios::binary);
		if (!input) { throw std::runtime_error(".gitignoreを読み込めません"); }
		std::string content((std::istreambuf_iterator<char>(input)), {});
		if (input.bad()) { throw std::runtime_error(".gitignoreの読み込みに失敗しました"); }
		return content;
	}
}

bool Engine::ProjectGitIgnoreService::IsBusy() const {

	return pending_.valid();
}

bool Engine::ProjectGitIgnoreService::HasError() const {

	return !state_.message.empty();
}

std::string Engine::ProjectGitIgnoreService::Poll() {

	if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
		return {};
	}
	auto result = pending_.get();
	state_ = std::move(result.state);
	refreshed_ = std::chrono::steady_clock::now();
	return result.message;
}

const Engine::ProjectGitIgnoreState* Engine::ProjectGitIgnoreService::Request(const std::filesystem::path& path, bool directory) {

	const bool same = state_.target == path && state_.directory == directory;
	// メニュー表示中の状態取得は短い間隔で再利用する
	if (!IsBusy() && (!same || std::chrono::steady_clock::now() - refreshed_ > std::chrono::seconds(2))) {
		pending_ = std::async(std::launch::async, [path, directory] {
			return Result{ Query(path, directory), {} };
		});
	}
	return same ? &state_ : nullptr;
}

void Engine::ProjectGitIgnoreService::SetIncluded(bool included) {

	if (IsBusy() || !state_.valid || (included && state_.parentExcluded)) { return; }
	pending_ = std::async(std::launch::async, [path = state_.target, directory = state_.directory, included] {
		return Apply(path, directory, included);
	});
}

Engine::ProjectGitIgnoreState Engine::ProjectGitIgnoreService::Query(const std::filesystem::path& path, bool directory) {

	ProjectGitIgnoreState state{};
	state.target = path;
	state.directory = directory;
	try {
		// 入れ子のRepositoryやworktreeもGit自身に解決させる
		const auto root = RunGit(directory ? path : path.parent_path(), { "rev-parse", "--show-toplevel" });
		if (root.code != 0) { throw std::runtime_error("この項目はGit Repositoryにありません"); }
		auto rootPath = root.output;
		while (!rootPath.empty() && (rootPath.back() == '\r' || rootPath.back() == '\n')) { rootPath.pop_back(); }
		state.repository = std::filesystem::weakly_canonical(Algorithm::PathFromUTF8(rootPath));
		const auto target = std::filesystem::weakly_canonical(path);
		if (!StorageFileUtility::IsInside(target, state.repository) || target == state.repository) {
			throw std::runtime_error("Repositoryのルートや外部の項目は変更できません");
		}
		// Windowsの大小文字差で親を見失わないように相対パスを組む
		std::filesystem::path relative;
		const auto rootKey = StorageFileUtility::PathKey(state.repository);
		for (auto current = target; StorageFileUtility::PathKey(current) != rootKey; current = current.parent_path()) {
			if (current == current.parent_path()) { throw std::runtime_error("Repository内のパスを解決できません"); }
			relative = current.filename() / relative;
		}
		if (relative.filename().empty()) { relative = relative.parent_path(); }
		state.relativePath = Algorithm::ConvertString(relative.generic_wstring());
		// 除外済みフォルダーの配下は個別に含められない
		for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path()) {
			if (IsIgnored(state.repository, Algorithm::ConvertString(parent.generic_wstring()))) {
				state.parentExcluded = true;
				break;
			}
		}
		state.targetExcluded = IsIgnored(state.repository, state.relativePath);
		state.metaExcluded = IsIgnored(state.repository, state.relativePath + ".meta");
		state.excluded = state.targetExcluded || state.metaExcluded || state.parentExcluded;
		const auto tracked = RunGit(state.repository, { "--literal-pathspecs", "ls-files", "--", state.relativePath, state.relativePath + ".meta" });
		if (tracked.code != 0) { throw std::runtime_error("Gitの追跡状態を取得できません"); }
		state.tracked = !tracked.output.empty();
		state.valid = true;
	} catch (const std::exception& exception) {
		state.message = exception.what();
	}
	return state;
}

Engine::ProjectGitIgnoreService::Result Engine::ProjectGitIgnoreService::Apply(
	const std::filesystem::path& path, bool directory, bool included) {

	Result result{};
	// 複数のProjectPanelからの保存を直列化する
	static std::mutex writeMutex;
	std::lock_guard lock(writeMutex);
	result.state = Query(path, directory);
	if (!result.state.valid || (included && result.state.parentExcluded)) {
		result.message = result.state.valid ? "親フォルダーの除外を先に解除してください" : result.state.message;
		result.state.message = result.message;
		return result;
	}
	try {
		const auto file = result.state.repository / ".gitignore";
		const std::string original = ReadDocument(file);
		std::string updated, error;
		if (!ProjectGitIgnoreDocument::Update(original, result.state.relativePath, directory, included, updated, error)) {
			throw std::runtime_error(error);
		}
		// 外部編集を検出してから既存の原子保存を使う
		if (ReadDocument(file) != original || !StorageFileUtility::WriteBytes(file, updated)) {
			throw std::runtime_error(".gitignoreが変更されたか、保存に失敗しました");
		}
		const auto refreshed = Query(path, directory);
		if (!refreshed.valid || refreshed.targetExcluded == included || refreshed.metaExcluded == included) {
			// より優先度の高い除外規則がある場合は元へ戻す
			if (ReadDocument(file) != updated || !StorageFileUtility::WriteBytes(file, original)) {
				throw std::runtime_error("除外設定の復元に失敗しました。.gitignoreを確認してください");
			}
			throw std::runtime_error("他の.gitignoreの規則が優先されています。該当する規則を確認してください");
		}
		result.state = refreshed;
		result.message = included ? "Gitの除外設定を解除しました。ステージ操作は手動で行ってください" :
			"Gitの除外設定を保存しました。追跡済みファイルの索引解除は手動で行ってください";
	} catch (const std::exception& exception) {
		result.message = exception.what();
		result.state = Query(path, directory);
		result.state.message = result.message;
	}
	return result;
}
