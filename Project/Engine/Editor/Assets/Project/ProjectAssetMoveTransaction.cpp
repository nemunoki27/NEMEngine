#include "ProjectAssetMoveTransaction.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	ProjectAssetMoveTransaction classMethods
//============================================================================
Engine::ProjectAssetMoveTransaction::~ProjectAssetMoveTransaction() {

	// 例外で抜けた場合も適用済みの移動を戻す
	if (!committed_ && !rolledBack_) {
		Rollback();
	}
}

bool Engine::ProjectAssetMoveTransaction::Add(const std::filesystem::path& source, const std::filesystem::path& target) {

	if (started_ || committed_ || rolledBack_ || source.empty() || target.empty()) {
		return false;
	}
	const auto sourceKey = StorageFileUtility::PathKey(source);
	const auto targetKey = StorageFileUtility::PathKey(target);
	if (sourceKey == targetKey) {
		return false;
	}
	// 同じパスの二重移動や循環する移動を登録しない
	for (const auto& entry : entries_) {
		const auto entrySource = StorageFileUtility::PathKey(entry.source);
		const auto entryTarget = StorageFileUtility::PathKey(entry.target);
		if (entrySource == sourceKey || entrySource == targetKey || entryTarget == sourceKey || entryTarget == targetKey) {
			return false;
		}
	}
	entries_.push_back({source, target});
	return true;
}

bool Engine::ProjectAssetMoveTransaction::Execute(std::string& diagnostic) {

	if (entries_.empty() || started_ || committed_ || rolledBack_) {
		diagnostic = "Assetの移動計画が無効です";
		return false;
	}
	// 全ての移動先を変更前に確認する
	std::error_code error;
	for (const auto& entry : entries_) {
		if (!std::filesystem::exists(entry.source, error) || error || std::filesystem::exists(entry.target, error) || error) {
			diagnostic = "移動元が見つからないか、移動先が使用されています";
			return false;
		}
	}
	started_ = true;
	for (const auto& entry : entries_) {
		if (!StorageFileUtility::MoveWithoutReplacement(entry.source, entry.target, error)) {
			diagnostic = "Assetまたは付随ファイルを移動できません";
			return false;
		}
		// 移動後に履歴のメモリ確保を行わない
		++movedCount_;
	}
	return true;
}

void Engine::ProjectAssetMoveTransaction::Commit() {

	// 全ファイルを移動した操作だけを確定する
	if (started_ && !rolledBack_ && movedCount_ == entries_.size()) {
		committed_ = true;
	}
}

bool Engine::ProjectAssetMoveTransaction::Rollback() noexcept {

	if (committed_ || rolledBack_) {
		return false;
	}
	rolledBack_ = true;
	bool restored = true;
	// 復元先に新しいファイルがあれば上書きせず診断する
	while (movedCount_ > 0) {
		const auto& entry = entries_[--movedCount_];
		std::error_code error;
		if (!StorageFileUtility::MoveWithoutReplacement(entry.target, entry.source, error)) {
			restored = false;
			try {
				Logger::Output(LogType::Engine, spdlog::level::err, "Assetの移動を取り消せません path={} 戻し先={} 詳細={}",
					Algorithm::PathToUTF8(entry.target), Algorithm::PathToUTF8(entry.source), error.message());
			} catch (...) {
				// 診断に失敗しても残りの復元を続ける
			}
		}
	}
	return restored;
}
