#include "ProductContentWriter.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Assets/Formats/VectorFieldData.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Rendering/Meshes/Import/GLTFFileReference.h>
#include <Engine/Core/Assets/Package/ProductAssetCook.h>
#include <Engine/Core/Assets/Package/ProductBinary.h>
#include <Engine/Core/Assets/Package/ProductContentPack.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace Engine::ProductContentWriter {

	namespace {

		using Path = std::filesystem::path;
		using Json = nlohmann::json;

		Json LoadDocument(const Path& path) {

			Json data;
			std::string diagnostic;
			if (!JsonFile::TryLoad(path, data, &diagnostic)) throw std::runtime_error(diagnostic);
			return data;
		}

		std::string Key(const Path& path) {
			return Algorithm::ConvertString(path.generic_wstring());
		}

		bool IsCookInput(const Path& file) {

			const auto name = Algorithm::ToLower(Algorithm::PathToUTF8(file.filename()));
			const auto extension = Algorithm::ToLower(Algorithm::PathToUTF8(file.extension()));
			return extension == ".meta" || extension == ".mtl" || extension == ".cs" ||
				extension == ".hlsl" || extension == ".hlsli" || name.ends_with(".shadergraph.json") ||
				name.starts_with("license.") || name == "license" || name.starts_with("copying.");
		}

		std::vector<uint8_t> ReadBytes(const Path& path) {

			std::ifstream input(path, std::ios::binary | std::ios::ate);
			const std::streamoff size = input.tellg();
			if (size < 0) throw std::runtime_error("製品データを開けません: " + Key(path));
			std::vector<uint8_t> bytes(static_cast<size_t>(size));
			input.seekg(0);
			if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("製品データを読めません: " + Key(path));
			return bytes;
		}

		struct References {
			AssetDependencyScanner::IDReferences ids;
			AssetDependencyScanner::PathReferences paths;
		};

		std::vector<uint8_t> Cook(const Path& stage, const Path& file, const Path& source, const AssetMeta& meta, References& references) {

			if (meta.type == AssetType::Mesh || meta.type == AssetType::Texture || meta.type == AssetType::Audio) {
				std::vector<uint8_t> bytes;
				std::string error;
				if (!ProductAssetCook::Convert(source, meta, bytes, error)) {
					throw std::runtime_error("製品アセットの変換に失敗しました: " + Key(source) + " / " + error);
				}
				return bytes;
			}
			if (Algorithm::ToLower(Key(file.extension())) == ".fga") {
				std::ifstream input(file);
				VectorFieldData field;
				std::string reason;
				if (!ReadFGA(input, field, reason)) throw std::runtime_error(Key(file) + ": " + reason);
				std::vector<uint8_t> bytes;
				WriteProductVectorField(field, bytes);
				return bytes;
			}
			if (meta.type == AssetType::Text) return ReadBytes(file);
			if (Algorithm::ToLower(Algorithm::PathToUTF8(file.extension())) == ".json" ||
				AssetTypeResolver::IsJsonAssetType(meta.type)) {
				const auto document = LoadDocument(file);
				if (meta.guid && AssetTypeResolver::IsJsonAssetFile(meta.type, file)) {
					if (meta.type == AssetType::Scene && document.contains("ExternalActors")) {
						const auto relative = file.lexically_relative(stage);
						auto part = relative.begin();
						auto folder = stage / *part;
						if (*part == L"Packages" || *part == L"Engine") folder /= *++part;
						const auto actors = folder / "ExternalActors" / ToString(meta.guid);
						for (const auto& actor : document.at("ExternalActors")) {
							const auto id = actor.get<std::string>();
							if (!TryParseUUID16Hex(id)) throw std::runtime_error("ExternalActorのIDが不正です: " + Key(file));
							AssetDependencyScanner::ScanReferences(LoadDocument(actors / (id + ".actor.json")), references.ids, references.paths);
						}
					}
					AssetDependencyScanner::ScanReferences(document, references.ids, references.paths);
				}
				return Json::to_cbor(document);
			}
			return ReadBytes(file);
		}

		std::string AssetPath(const std::string& path) {
			if (path.starts_with("Packages/")) return "package://" + path.substr(9);
			return path;
		}
	}

	bool Write(const Path& stage, const Path& output, const Path& manifest, std::string& error) {

		Path partial;
		bool created = false;
		try {
			const auto root = std::filesystem::absolute(stage).lexically_normal();
			const auto target = std::filesystem::absolute(output).lexically_normal();
			if (std::filesystem::exists(target)) throw std::runtime_error("出力先は既に存在します");
			std::unordered_map<std::string, Path> sources;
			std::unordered_map<std::string, std::string> reasons;
			if (!manifest.empty()) {
				const auto inputs = LoadDocument(manifest);
				std::filesystem::current_path(Algorithm::PathFromUTF8(inputs.at("gameRoot").get<std::string>()));
				for (const auto& entry : inputs.at("files")) {
					reasons.emplace(entry.at("destination").get<std::string>(), entry.value("reason", ""));
					sources.emplace(Key(Algorithm::PathFromUTF8(entry.at("destination").get<std::string>())),
						Algorithm::PathFromUTF8(entry.at("source").get<std::string>()));
				}
			} else {
				std::filesystem::current_path(root);
			}
			RuntimePaths::Refresh();

			Json index = {{"files", Json::array()}};
			for (const auto& file : std::filesystem::directory_iterator(root)) {
				if (file.path().extension() == L".nemproject") {
					if (index.contains("projectGuid")) throw std::runtime_error("製品のプロジェクト設定が複数あります");
					index["projectGuid"] = LoadDocument(file.path()).at("projectGuid");
				}
			}
			if (!index.contains("projectGuid")) throw std::runtime_error("製品のプロジェクト設定がありません");

			std::vector<Path> paths;
			std::unordered_set<std::string> modelBuffers;
			for (const char* directory : {"Engine/Assets", "GameAssets", "ProjectSettings", "Packages", "Cooked"}) {
				if (!std::filesystem::is_directory(root / directory)) continue;
				for (const auto& file : std::filesystem::recursive_directory_iterator(root / directory)) {
					if (!file.is_regular_file() || IsCookInput(file.path())) continue;
					paths.emplace_back(file.path());
					if (Algorithm::ToLower(Key(file.path().extension())) == ".gltf") {
						const auto model = LoadDocument(file.path());
						for (const auto& buffer : model.value("buffers", Json::array())) {
							const std::string uri = buffer.value("uri", "");
							if (!uri.empty() && !uri.starts_with("data:")) {
								modelBuffers.emplace(Algorithm::ToLower(Key((file.path().parent_path() / GLTFFileReference::Decode(uri)).lexically_normal())));
							}
						}
					}
				}
			}
			std::unordered_set<std::string> bufferHashes;
			for (const auto& file : paths) {
				if (modelBuffers.contains(Algorithm::ToLower(Key(file)))) {
					bufferHashes.emplace(ContentHash::FileSHA256(file));
				}
			}
			std::ranges::sort(paths);
			std::filesystem::create_directories(target.parent_path());
			partial = target.parent_path() / (ToString(AssetGUID::New()) + ".partial");
			std::ofstream stream(partial, std::ios::binary | std::ios::trunc);
			if (!stream) throw std::runtime_error("製品パックを作成できません");
			created = true;
			stream.exceptions(std::ios::badbit | std::ios::failbit);
			ProductContentPack::Header header;
			header.magic = ProductContentPack::kCompressedMagic;
			stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
			std::unordered_map<std::string, References> references;
			std::unordered_map<std::string, AssetID> byPath;
			struct Blob { uint64_t offset; uint64_t compressedSize; bool textureSource; };
			std::unordered_map<std::string, Blob> blobs;
			for (const auto& file : paths) {
				if (modelBuffers.contains(Algorithm::ToLower(Key(file)))) continue;
				if (Algorithm::ToLower(Key(file.extension())) == ".bin" &&
					bufferHashes.contains(ContentHash::FileSHA256(file))) continue;
				const std::string path = Key(file.lexically_relative(root));
				AssetMeta meta;
				const auto sidecar = AssetMetaStorage::MetaPathOf(file);
				Json entry = {{"path", path}};
				if (std::filesystem::is_regular_file(sidecar)) {
					if (!AssetMetaStorage::ParseMeta(LoadDocument(sidecar), meta)) {
						throw std::runtime_error("アセットのメタデータが不正です: " + path);
					}
					meta.assetPath = AssetPath(path);
					byPath.emplace(Algorithm::ToLower(meta.assetPath), meta.guid);
					entry["asset"] = {{"guid", ToString(meta.guid)}, {"path", meta.assetPath}, {"type", meta.type},
						{"importer", meta.importer}, {"version", meta.importerVersion}, {"settings", meta.importerSettings}};
				}
				const auto original = sources.find(path);
				const auto& source = original != sources.end() && meta.type == AssetType::Mesh ? original->second : file;
				const auto bytes = Cook(root, file, source, meta, references[path]);
				if (meta.type == AssetType::Text) entry["text"] = true;
				entry["size"] = bytes.size();
				const auto digest = ContentHash::SHA256(bytes);
				if (digest.size() != header.indexHash.size()) throw std::runtime_error("データのハッシュを計算できません: " + path);
				const auto [item, added] = blobs.try_emplace(digest, Blob{static_cast<uint64_t>(stream.tellp()), 0, false});
				if (added) {
					auto compressed = ProductBinary::CompressSmaller(bytes);
					if (meta.type == AssetType::Texture) {
						std::vector<uint8_t> packedTexture;
						item->second.textureSource = ProductAssetCook::TryTextureSource(file, meta, bytes,
							compressed.empty() ? bytes.size() : compressed.size(), packedTexture);
						if (item->second.textureSource) compressed.swap(packedTexture);
					}
					item->second.compressedSize = compressed.size();
					const auto& payload = compressed.empty() ? bytes : compressed;
					stream.write(reinterpret_cast<const char*>(payload.data()), payload.size());
				}
				entry["hash"] = digest;
				entry["offset"] = item->second.offset;
				if (item->second.compressedSize) entry[item->second.textureSource ? "textureSourceSize" : "lzmsSize"] = item->second.compressedSize;
				index["files"].push_back(std::move(entry));
			}
			for (auto& file : index["files"]) {
				const auto asset = file.find("asset");
				if (asset == file.end()) continue;
				const auto& refs = references.at(file.at("path").get<std::string>());
				std::vector<std::string> dependencies;
				for (const auto& ref : refs.ids) {
					if (ref.second != AssetType::Script) dependencies.emplace_back(ToString(ref.first));
				}
				for (const auto& ref : refs.paths) {
					const auto found = byPath.find(Algorithm::ToLower(ref.first));
					if (found != byPath.end()) dependencies.emplace_back(ToString(found->second));
				}
				std::ranges::sort(dependencies);
				dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
				if (!dependencies.empty()) (*asset)["dependencies"] = std::move(dependencies);
			}
			header.indexOffset = static_cast<uint64_t>(stream.tellp());
			const auto bytes = Json::to_cbor(index);
			const auto hash = ContentHash::SHA256(bytes);
			if (hash.size() != header.indexHash.size()) throw std::runtime_error("索引のハッシュを計算できません");
			std::copy(hash.begin(), hash.end(), header.indexHash.begin());
			stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			stream.seekp(0);
			stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
			stream.close();
			std::filesystem::rename(partial, target);
			std::map<std::string, uint64_t> totals;
			std::vector<std::pair<uint64_t, std::string>> largest;
			for (const auto& entry : index.at("files")) {
				const auto blob = blobs.find(entry.at("hash").get<std::string>());
				if (blob == blobs.end()) continue;
				const auto size = blob->second.compressedSize ? blob->second.compressedSize : entry.at("size").get<uint64_t>();
				blobs.erase(blob);
				const auto path = entry.at("path").get<std::string>();
				const std::string type = entry.contains("asset") ? std::string(EnumAdapter<AssetType>::ToString(entry.at("asset").at("type").get<AssetType>())) :
					path.starts_with("Cooked/") ? "CookedShader" : "RuntimeData";
				totals[type] += size;
				largest.emplace_back(size, path);
			}
			for (const auto& [type, size] : totals) {
				std::cout << std::format("[NEM_GAME_BUILD_SIZE] {}: {:.2f} MB\n", type, size / 1e6);
			}
			std::ranges::sort(largest, std::greater{});
			for (size_t i = 0; i < (std::min)(largest.size(), size_t{10}); ++i) {
				const auto& [size, path] = largest[i];
				const auto found = reasons.find(path);
				const std::string reason = found != reasons.end() && !found->second.empty() ? found->second :
					path.starts_with("Cooked/") ? "製品用シェーダー" : "ランタイム設定・依存ファイル";
				std::cout << std::format("[NEM_GAME_BUILD_SIZE] {:.2f} MB  {}  ({})\n", size / 1e6, path, reason);
			}
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			std::error_code ec;
			if (created) std::filesystem::remove(partial, ec);
			return false;
		}
	}
}
