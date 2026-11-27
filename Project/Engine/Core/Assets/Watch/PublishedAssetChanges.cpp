#include "PublishedAssetChanges.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Windows.h>
#include <deque>
#include <optional>

namespace {

	using namespace Engine;

	struct FileVersion {
		DWORD volume, indexHigh, indexLow;
		LONGLONG changed, modified;
		bool operator==(const FileVersion&) const = default;
	};

	std::optional<FileVersion> GetVersion(const std::filesystem::path& path) {
		const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle == INVALID_HANDLE_VALUE) return {};
		BY_HANDLE_FILE_INFORMATION id{};
		FILE_BASIC_INFO times{};
		const bool ok = GetFileInformationByHandle(handle, &id) &&
			GetFileInformationByHandleEx(handle, FileBasicInfo, &times, sizeof(times));
		CloseHandle(handle);
		if (!ok) return {};
		return FileVersion{id.dwVolumeSerialNumber, id.nFileIndexHigh, id.nFileIndexLow, times.ChangeTime.QuadPart, times.LastWriteTime.QuadPart};
	}

	struct Publication {
		AssetID asset;
		uint64_t revision;
		std::weak_ptr<const uint8_t> database;
		std::vector<std::pair<std::filesystem::path, FileVersion>> files;
	};
	std::deque<Publication> publications;
}

Engine::AssetID Engine::PublishedAssetChanges::Import(AssetDatabase& database, const std::string& path, AssetType type) {

	const auto before = database.GetStructureRevision();
	const AssetID asset = database.ImportOrGet(path, type);
	if (!asset || before == database.GetStructureRevision()) return asset;

	Publication publication{asset, database.GetStructureRevision(), database.GetCacheLifetime(), {}};
	const auto full = database.ResolveFullPath(asset).lexically_normal();
	const auto record = [&](const std::filesystem::path& file) {
		if (auto version = GetVersion(file)) publication.files.emplace_back(file, *version);
	};
	record(full);
	record(AssetMetaStorage::MetaPathOf(full));
	const auto root = RuntimePaths::GetGameAssetsRoot().lexically_normal();
	for (auto dir = full.parent_path(); !dir.empty() && dir != root && dir != dir.root_path(); dir = dir.parent_path()) record(dir);
	publications.emplace_back(std::move(publication));
	if (publications.size() > 128) publications.pop_front();
	return asset;
}

bool Engine::PublishedAssetChanges::Matches(const AssetDatabase& database, const std::filesystem::path& path) {

	auto normalized = path.lexically_normal();
	// 原子的保存に使用した一時ファイルの通知は公開先の版と照合する
	if (normalized.extension() == ".tmp") {
		std::error_code ec;
		if (std::filesystem::exists(normalized, ec) || ec) return false;
		normalized.replace_extension();
		const auto suffix = Algorithm::PathToUTF8(normalized.extension());
		if (suffix.size() != 33 || !TryParseAssetGUID32Hex(suffix.substr(1))) return false;
		normalized.replace_extension();
	}
	const auto owner = database.GetCacheLifetime().lock();
	for (auto it = publications.rbegin(); it != publications.rend(); ++it) {
		if (it->database.lock() != owner) continue;
		for (const auto& [file, version] : it->files) {
			if (normalized == file) return GetVersion(file) == version;
		}
	}
	return false;
}

bool Engine::PublishedAssetChanges::CollectAdditions(const AssetDatabase& database, uint64_t previous, std::vector<AssetID>& additions) {

	const auto owner = database.GetCacheLifetime().lock();
	for (const auto& entry : publications) {
		if (entry.revision <= previous || entry.database.lock() != owner) continue;
		if (entry.revision != previous + 1) return false;
		additions.emplace_back(entry.asset);
		previous = entry.revision;
	}
	return previous == database.GetStructureRevision();
}
