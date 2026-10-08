#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <array>
#include <cstdint>
#include <string>
#include <system_error>

namespace Engine::StorageFileUtility {

	using Path = std::filesystem::path;

	// 移動前後で同じファイルを照合する識別情報
	struct FileIdentity {

		uint64_t volumeID = 0;			  // 保存ボリュームの識別子
		std::array<uint8_t, 16> fileID{}; // ファイルの識別子

		bool operator==(const FileIdentity&) const = default;
	};

	// 保存先の比較と復旧前後の照合
	std::string PathKey(const Path& path);
	bool IsInside(const Path& path, const Path& root);
	std::string FileRevision(const Path& path);

	// 全byteを書き終えてから保存先を置き換える
	bool WriteBytes(const Path& path, const std::string& serialized);
	// 既存ファイルを置き換えず、作成したhandleの所有を返す
	bool WriteBytesWithoutReplacement(const Path& path, const std::string& serialized, FileIdentity& identity);
	// 読み込んだbyteの内容を照合する
	std::string ReadVerifiedBytes(const Path& path, const std::string& revision);
	// コピー元を保持して非置換で作成し、その所有を返す
	bool CopyFileWithoutReplacement(const Path& source, const Path& target, FileIdentity& identity, std::string& revision);
	bool CopyFileAtomically(const Path& source, const Path& target);
	// 移動先に現れた既存ファイルを置き換えない
	bool MoveWithoutReplacement(const Path& source, const Path& target, std::error_code& error) noexcept;
	// ファイルやフォルダーの識別情報を取得する
	bool ReadIdentity(const Path& path, FileIdentity& identity, std::error_code& error) noexcept;
	// 識別情報を排他照合した対象だけを非置換で移動する
	bool MoveIfIdentity(const Path& source, const Path& target, const FileIdentity& identity, std::error_code& error) noexcept;
	// 所有と内容を排他照合したファイルだけを非置換で移動する
	bool MoveIfRevision(const Path& source, const Path& target, const std::string& revision, const FileIdentity& identity,
		std::error_code& error) noexcept;
	// 外部変更のないファイルだけを排他取得して削除する
	bool RemoveIfRevision(const Path& path, const std::string& revision, std::error_code& error);
	// 所有したファイルの識別情報と内容を同じhandleで照合する
	bool RemoveIfRevision(const Path& path, const std::string& revision, const FileIdentity& identity, std::error_code& error);
	// 所有した対象だけを削除する、フォルダーは空の場合に限る
	bool RemoveIfIdentity(const Path& path, const FileIdentity& identity, std::error_code& error);
}
