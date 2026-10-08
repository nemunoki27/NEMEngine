#pragma once

//============================================================================
//	include
//============================================================================
#include "JsonFileJournal.h"
#include <Engine/Core/Foundation/Serialization/StorageFilePublication.h>
#include <Engine/Core/Foundation/Serialization/StorageDirectoryLease.h>

// c++
#include <memory>

namespace Engine::JsonFileJournal::Internal {

	//============================================================================
	//	ScopeLock class
	//	保存範囲の排他を処理終了まで保持する
	//============================================================================
	class ScopeLock {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 保存範囲の排他を取得する
		explicit ScopeLock(const Scope& scope);
		~ScopeLock();
		ScopeLock(const ScopeLock&) = delete;
		ScopeLock& operator=(const ScopeLock&) = delete;

		// 復旧へ処理を渡す前に排他を解除する
		void Release();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		void* file_;									   // 保存範囲の排他保持
		std::unique_ptr<StorageDirectoryLease> directory_; // 操作中の保存範囲
	};

	// 読込と内容照合を一組で行う
	using StorageFileUtility::ReadVerifiedBytes;
	// 本体と退避記録から再開可能な操作を読む
	nlohmann::json ReadJournal(const std::filesystem::path& directory);
	// 操作中のフォルダー差替えを防ぐ
	StorageDirectoryLease LeaseDirectory(const std::filesystem::path& directory);
	// 復旧先と退避元を適用直前にも照合する
	std::filesystem::path ValidateRecoveryFile(const Scope& scope, const std::filesystem::path& directory,
		const nlohmann::json& entry, bool pending, bool checkCurrent = true, std::string* revision = nullptr);
	// 公開用の所有を操作記録へ保存する
	nlohmann::json EncodePublication(const StorageFilePublicationRecord& record);
	// 公開用の所有記録を検証して読む
	StorageFilePublicationRecord DecodePublication(const nlohmann::json& data);
	// 記録した所有に元ファイルが残る中断だけを認める
	bool HasRetiredRevision(const std::filesystem::path& target, const nlohmann::json& entry, const std::string& revision);
	// 確定後に所有した準備と退避だけを回収する
	void CleanupPublications(const nlohmann::json& journal);
}
