#include "ProductContentPack.h"
#include "ProductAssetCook.h"
#include "ProductBinary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Engine::ProductContentPack {

	namespace {

		struct Entry {
			uint64_t offset;
			uint64_t size;
			std::string hash;
			bool text = false;
			uint64_t storedSize = 0;
			bool textureSource = false;
		};
		struct Archive {
			std::filesystem::path path;
			std::filesystem::path root;
			std::string guid;
			std::unordered_map<std::string, Entry> files;
			std::vector<AssetMeta> assets;
		};
		// 起動時に一度マウントし、以降は各読込スレッドから参照する
		Archive mounted;

		std::string Key(const std::filesystem::path& path, const std::filesystem::path& root) {

			if (path.empty()) return {};
			const auto normalized = path.lexically_normal();
			std::string key = Algorithm::ToLower(Algorithm::ConvertString(normalized.generic_wstring()));
			if (normalized.is_absolute()) {
				auto prefix = Algorithm::ToLower(Algorithm::ConvertString(root.generic_wstring()));
				if (prefix.empty()) return {};
				if (!prefix.ends_with('/')) prefix.push_back('/');
				if (!key.starts_with(prefix)) return {};
				key.erase(0, prefix.size());
			} else if (normalized.has_root_path() || *normalized.begin() == L"..") {
				return {};
			}
			for (const auto& [scheme, directory] : {
				std::pair<std::string_view, std::string_view>{"package:/", "packages/"},
				{"engine:/", "engine/assets/"}, {"game:/", "gameassets/"}}) {
				if (key.starts_with(scheme)) {
					auto rest = key.substr(scheme.size());
					if (rest.starts_with('/')) rest.erase(0, 1);
					return std::string(directory) + rest;
				}
			}
			return key;
		}

		bool ReadEntry(std::ifstream& stream, const Entry& entry, std::vector<uint8_t>& bytes) {

			std::vector<uint8_t> result(static_cast<size_t>(entry.storedSize ? entry.storedSize : entry.size));
			stream.seekg(static_cast<std::streamoff>(entry.offset));
			if (!stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()))) return false;
			if (entry.textureSource) {
				std::vector<uint8_t> decoded;
				if (!ProductAssetCook::RestoreTextureSource(result, static_cast<size_t>(entry.size), decoded)) return false;
				result.swap(decoded);
			} else if (entry.storedSize) {
				std::vector<uint8_t> decoded;
				if (!ProductBinary::Decompress(result, static_cast<size_t>(entry.size), decoded)) return false;
				result.swap(decoded);
			}
			if (ContentHash::SHA256(result) != entry.hash) return false;
			bytes = std::move(result);
			return true;
		}

		bool VerifyEntry(std::ifstream& stream, const Entry& entry) {

			stream.seekg(static_cast<std::streamoff>(entry.offset));
			if (entry.storedSize) {
				std::vector<uint8_t> decoded;
				return ReadEntry(stream, entry, decoded);
			}
			uint64_t remaining = entry.size;
			const auto hash = ContentHash::ReadSHA256([&](std::span<uint8_t> buffer, size_t& count) {
				count = static_cast<size_t>((std::min<uint64_t>)(remaining, buffer.size()));
				remaining -= count;
				return count == 0 || static_cast<bool>(stream.read(reinterpret_cast<char*>(buffer.data()), count));
			});
			return hash == entry.hash;
		}
		bool Open(const std::filesystem::path& path, Archive& archive, bool verifyPayload, std::string& error) {

			try {
				archive.path = std::filesystem::absolute(path).lexically_normal();
				archive.root = archive.path.parent_path();
				if (Algorithm::ToLower(Algorithm::PathToUTF8(archive.root.filename())) == "data") {
					archive.root = archive.root.parent_path();
				}
				std::ifstream input(archive.path, std::ios::binary | std::ios::ate);
				const std::streamoff end = input.tellg();
				Header header{};
				input.seekg(0);
				if (end < static_cast<std::streamoff>(sizeof(header)) ||
					!input.read(reinterpret_cast<char*>(&header), sizeof(header)) || (header.magic != Header{}.magic && header.magic != kCompressedMagic) ||
					header.indexOffset < sizeof(header) || static_cast<uint64_t>(end) <= header.indexOffset) {
					throw std::runtime_error("製品パックのヘッダーが不正です");
				}
				const uint64_t indexSize = static_cast<uint64_t>(end) - header.indexOffset;
				if (indexSize > 256ull * 1024 * 1024) throw std::runtime_error("製品パックの索引が大きすぎます");
				std::vector<uint8_t> bytes;
				if (!ReadEntry(input, {header.indexOffset, indexSize, std::string(header.indexHash.data(), header.indexHash.size())}, bytes)) {
					throw std::runtime_error("製品パックの索引が破損しています");
				}
				const auto index = nlohmann::json::from_cbor(bytes);
				archive.guid = index.at("projectGuid").get<std::string>();
				if (!TryParseAssetGUID32Hex(archive.guid)) throw std::runtime_error("製品のGUIDが不正です");
				std::unordered_set<AssetID> guids;
				std::unordered_map<std::string, Entry> blobs;
				uint64_t next = sizeof(header);
				for (const auto& file : index.at("files")) {
					const std::string name = file.at("path").get<std::string>();
					const std::string key = Key(Algorithm::PathFromUTF8(name), {});
					Entry entry{file.at("offset").get<uint64_t>(), file.at("size").get<uint64_t>(), file.at("hash").get<std::string>(), file.value("text", false)};
					if (header.magic == kCompressedMagic) {
						entry.textureSource = file.contains("textureSourceSize");
						if (entry.textureSource && file.contains("lzmsSize")) throw std::runtime_error("製品データの圧縮形式が重複しています");
						entry.storedSize = file.value(entry.textureSource ? "textureSourceSize" : "lzmsSize", uint64_t{});
						if (entry.textureSource && !entry.storedSize) throw std::runtime_error("製品テクスチャが空です");
					}
					if (entry.storedSize && (entry.storedSize >= entry.size || entry.size > 0x7fffffffULL)) throw std::runtime_error("製品パックの圧縮サイズが不正です");
					const uint64_t stored = entry.storedSize ? entry.storedSize : entry.size;
					const auto [blob, first] = blobs.emplace(entry.hash, entry);
					const bool validRange = first ? (entry.offset == next && stored <= header.indexOffset - next) :
						(entry.offset == blob->second.offset && entry.size == blob->second.size && entry.storedSize == blob->second.storedSize && entry.textureSource == blob->second.textureSource);
					if (key.empty() || !validRange ||
						entry.hash.size() != 64 || !archive.files.emplace(key, entry).second) {
						throw std::runtime_error("製品パックのファイル索引が不正です: " + name);
					}
					if (first) next += stored;
					if (first && verifyPayload && !VerifyEntry(input, entry)) {
						throw std::runtime_error("製品データの照合に失敗しました: " + name);
					}
					if (const auto meta = file.find("asset"); meta != file.end()) {
						AssetMeta asset;
						const auto guid = TryParseAssetGUID32Hex(meta->at("guid").get<std::string>());
						if (!guid || !guids.insert(*guid).second) throw std::runtime_error("アセットGUIDが不正です: " + name);
						asset.guid = *guid;
						asset.type = meta->at("type").get<AssetType>();
						asset.assetPath = meta->value("path", name);
						if (Key(Algorithm::PathFromUTF8(asset.assetPath), {}) != key) {
							throw std::runtime_error("製品アセットの論理パスが一致しません: " + name);
						}
						asset.importer = meta->at("importer").get<std::string>();
						asset.importerVersion = meta->at("version").get<uint32_t>();
						asset.importerSettings = meta->at("settings");
						if (const auto refs = meta->find("dependencies"); refs != meta->end()) {
							for (const auto& ref : *refs) {
								const auto id = TryParseAssetGUID32Hex(ref.get<std::string>());
								if (!id) throw std::runtime_error("製品アセットの依存GUIDが不正です: " + name);
								asset.dependencies.emplace_back(*id);
							}
						}
						archive.assets.emplace_back(std::move(asset));
					}
				}
				if (next != header.indexOffset) throw std::runtime_error("製品パックのデータサイズが一致しません");
				for (std::string_view required : {"packages/packages-lock.json", "projectsettings/runtime/game.json", "projectsettings/runtime/startupscene.json"}) {
					if (!archive.files.contains(std::string(required))) throw std::runtime_error("製品設定がありません: " + std::string(required));
				}
				return true;
			} catch (const std::exception& exception) {
				error = exception.what();
				return false;
			}
		}
	}

	bool Mount(const std::filesystem::path& path, std::string& error) {

		if (IsMounted()) {
			if (std::filesystem::absolute(path).lexically_normal() == mounted.path) return true;
			error = "別の製品パックが既にマウントされています";
			return false;
		}
		Archive archive;
		if (!Open(path, archive, false, error)) return false;
		mounted = std::move(archive);
		return true;
	}

	bool Verify(const std::filesystem::path& path, std::string& error, const std::string& expectedGuid) {

		Archive archive;
		if (!Open(path, archive, true, error)) return false;
		if (!expectedGuid.empty() && archive.guid != expectedGuid) {
			error = "出力先は別のプロジェクトの製品です";
			return false;
		}
		return true;
	}

	bool IsMounted() { return !mounted.path.empty(); }
	std::filesystem::path Root() { return mounted.root; }
	std::string ProjectGuid() { return mounted.guid; }
	const std::vector<AssetMeta>& Assets() { return mounted.assets; }

	bool Contains(const std::filesystem::path& path) {

		return IsMounted() && mounted.files.contains(Key(path, mounted.root));
	}

	std::optional<uint64_t> Size(const std::filesystem::path& path) {

		if (!IsMounted()) return std::nullopt;
		const auto entry = mounted.files.find(Key(path, mounted.root));
		if (entry == mounted.files.end()) return std::nullopt;
		return entry->second.size;
	}

	bool Read(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {

		if (!IsMounted()) return false;
		const auto entry = mounted.files.find(Key(path, mounted.root));
		if (entry == mounted.files.end()) return false;
		std::ifstream input(mounted.path, std::ios::binary);
		return input && ReadEntry(input, entry->second, bytes);
	}

	bool ReadJson(const std::filesystem::path& path, nlohmann::json& value) {

		std::vector<uint8_t> bytes;
		if (!Read(path, bytes)) return false;
		const auto& entry = mounted.files.at(Key(path, mounted.root));
		auto parsed = entry.text ? nlohmann::json::parse(bytes, nullptr, false) : nlohmann::json::from_cbor(bytes, true, false);
		if (parsed.is_discarded()) return false;
		value = std::move(parsed);
		return true;
	}
}
