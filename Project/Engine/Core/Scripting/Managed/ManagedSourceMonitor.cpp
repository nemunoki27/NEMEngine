#include "ManagedSourceMonitor.h"
#include "ManagedBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <algorithm>
#include <cwctype>

using namespace Engine::ManagedBuildUtility;

namespace {

	bool IsExcludedDirectory(const std::wstring& directoryName) {
		static const wchar_t* kExcluded[] = {
			L"bin", L"obj", L".git", L".vs", L"Generated", L"Library", L"Temp",
			L"Staging", L"Shadow", L"LastKnownGood",
		};
		for (const wchar_t* excluded : kExcluded) {
			if (_wcsicmp(directoryName.c_str(), excluded) == 0) {
				return true;
			}
		}
		return false;
	}

	bool IsGeneratedScriptFile(const std::wstring& fileName) {
		const auto endsWith = [&fileName](const wchar_t* suffix) {
			const size_t suffixLength = std::wcslen(suffix);
			if (fileName.size() < suffixLength) {
				return false;
			}
			return _wcsicmp(fileName.c_str() + (fileName.size() - suffixLength), suffix) == 0;
			};
		return endsWith(L".g.cs") || endsWith(L".generated.cs");
	}
}

void Engine::ManagedSourceMonitor::Reset() {

	hasSnapshot_ = false;
	sourceSnapshot_.clear();
	nextScanTime_ = std::chrono::steady_clock::time_point{};
}

void Engine::ManagedSourceMonitor::Stop() {

	watcher_.Stop();
	watchedRoot_.clear();
}

void Engine::ManagedSourceMonitor::Poll(const std::filesystem::path& projectPath, bool& dirty,
	std::chrono::steady_clock::time_point& lastChangeTime, int32_t& changedSourceCount) {

	const auto now = std::chrono::steady_clock::now();

	if (projectPath.empty()) {
		sourceSnapshot_.clear();
		hasSnapshot_ = false;
		watcher_.Stop();
		watchedRoot_.clear();
		return;
	}

	// Scriptsルートと兄弟のGameAssetsを含む共通の親を監視する
	const std::filesystem::path watchRoot = projectPath.parent_path().parent_path();
	if (watchRoot != watchedRoot_) {

		watcher_.Start(watchRoot);
		watchedRoot_ = watchRoot;
		// 張り直し直後は確実に一度走査するため安全走査の期限をリセットする
		nextScanTime_ = std::chrono::steady_clock::time_point{};
	}

	// 変更を検知したか取りこぼし対策の安全走査期限が来た時だけ実走査する
	const bool changedByWatcher = watcher_.ConsumeChanged();
	if (!changedByWatcher && now < nextScanTime_) {
		return;
	}
	nextScanTime_ = now + scanInterval_;

	// csproj単体+ Scriptsルート+ GameAssetsルートを集約する
	std::unordered_map<std::string, SourceStamp> current;
	const auto addFile = [&current](const std::filesystem::path& path) {
		std::error_code timeError{};
		std::error_code sizeError{};
		const auto time = std::filesystem::last_write_time(path, timeError);
		const auto size = std::filesystem::file_size(path, sizeError);
		if (!timeError && !sizeError) {
			current.emplace(ToUtf8Path(path.lexically_normal()), SourceStamp{ time, size });
		}
		};

	std::error_code projectExists{};
	if (std::filesystem::exists(projectPath, projectExists) && !projectExists) {
		addFile(projectPath);
	}

	const std::filesystem::path scriptsRoot = projectPath.parent_path();
	const std::filesystem::path gameAssetsRoot = scriptsRoot.parent_path() / "GameAssets";
	for (const std::filesystem::path& root : { scriptsRoot, gameAssetsRoot }) {

		std::error_code rootExists{};
		if (!std::filesystem::exists(root, rootExists) || rootExists) {
			continue;
		}
		// アクセス権エラーで監視全体を止めないようerror_code版で走査する
		std::error_code iterateError{};
		auto iterator = std::filesystem::recursive_directory_iterator(
			root, std::filesystem::directory_options::skip_permission_denied, iterateError);
		const std::filesystem::recursive_directory_iterator end{};
		for (; iterator != end; iterator.increment(iterateError)) {

			if (iterateError) {
				iterateError.clear();
				continue;
			}
			const std::filesystem::directory_entry& entry = *iterator;
			std::error_code statusError{};
			if (entry.is_directory(statusError) && !statusError) {
				if (IsExcludedDirectory(entry.path().filename().wstring())) {
					iterator.disable_recursion_pending();
				}
				continue;
			}
			if (!entry.is_regular_file(statusError) || statusError) {
				continue;
			}
			const std::filesystem::path& filePath = entry.path();
			if (filePath.extension() != ".cs") {
				continue;
			}
			if (IsGeneratedScriptFile(filePath.filename().wstring())) {
				continue;
			}
			addFile(filePath);
		}
	}

	if (!hasSnapshot_) {
		sourceSnapshot_ = std::move(current);
		hasSnapshot_ = true;
		return;
	}

	// 変更判定でサイズと更新時刻と追加削除を見る
	bool changed = current.size() != sourceSnapshot_.size();
	int32_t changedCount = 0;
	if (!changed) {
		for (const auto& [path, stamp] : current) {
			auto it = sourceSnapshot_.find(path);
			if (it == sourceSnapshot_.end() || it->second.time != stamp.time || it->second.size != stamp.size) {
				changed = true;
				++changedCount;
			}
		}
	} else {
		changedCount = static_cast<int32_t>(current.size() > sourceSnapshot_.size()
			? current.size() - sourceSnapshot_.size() : sourceSnapshot_.size() - current.size());
	}

	sourceSnapshot_ = std::move(current);
	if (changed) {

		dirty = true;
		lastChangeTime = now;
		changedSourceCount = changedCount;
	}
}

bool Engine::ManagedSourceMonitor::IsNewerThan(const std::filesystem::path& assemblyPath) const {

	// 起動時に実際にロードされるアセンブリ自体の時刻を基準にする
	// Edit中ビルドはステージングへ出るためロード元のcanonicalは更新されない、ここはロード対象そのものを見る
	std::error_code existsError{};
	if (assemblyPath.empty() || !std::filesystem::exists(assemblyPath, existsError) || existsError) {
		// ロード済みアセンブリが無ければ新旧を判断できないのでPlay側のビルドに任せる
		return false;
	}
	std::error_code timeError{};
	const auto assemblyTime = std::filesystem::last_write_time(assemblyPath, timeError);
	if (timeError) {
		return false;
	}

	// 監視中ソースのどれかがロード対象より新しければ起動前に編集されたとみなす
	for (const auto& [path, stamp] : sourceSnapshot_) {
		if (stamp.time > assemblyTime) {
			return true;
		}
	}
	return false;
}
