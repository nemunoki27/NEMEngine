#include "ModelDocumentReferenceTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/GLTFDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Assets/Project/ProjectAssetCopyUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetMoveUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetDocumentPatch.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>

// c++
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// json
#include <json.hpp>

namespace {

	// アドレス演算の独自定義を持つ要素
	struct AddressProbe {

		int value = 0; // 列挙する値
		AddressProbe* operator&() { return nullptr; }
		const AddressProbe* operator&() const { return nullptr; }
	};

	// 元の所有一覧を公開するAPIがないことを確認する
	template <typename Range>
	constexpr bool kExposesStorage = requires(const Range& range) { range.base(); };

	// GLBの整数を整列に依存せず追加する
	void AppendUInt32(std::string& bytes, uint32_t value) {

		bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	// 検証用JSONと任意のBinaryをGLBへまとめる
	std::string MakeBinary(std::string json, const std::string& payload) {

		json.append((4 - json.size() % 4) % 4, ' ');
		std::string bytes;
		AppendUInt32(bytes, 0x46546C67);
		AppendUInt32(bytes, 2);
		AppendUInt32(bytes, static_cast<uint32_t>(28 + json.size() + payload.size()));
		AppendUInt32(bytes, static_cast<uint32_t>(json.size()));
		AppendUInt32(bytes, 0x4E4F534A);
		bytes += json;
		AppendUInt32(bytes, static_cast<uint32_t>(payload.size()));
		AppendUInt32(bytes, 0x004E4942);
		bytes += payload;
		return bytes;
	}
}

bool NEMTests::CheckModelDocumentReferences() {

	using namespace Engine;
	// OBJの行末と空白を保ちMaterial名だけを更新する
	const std::string obj = "# mtllib ignored.mtl\r\n\tmtllib materials/共有 Material.mtl\r\nusemtl body\nmtllib other.mtl";
	std::string objBytes = obj;
	std::string objDiagnostic;
	std::vector<std::string> objReferences;
	if (!ModelDocumentReferences::Rewrite(
			"model.obj", objBytes,
			[&](std::string& reference) {
				objReferences.push_back(reference);
				reference = "../" + reference;
				return true;
			},
			objDiagnostic) ||
		objReferences != std::vector<std::string>{"materials/共有 Material.mtl", "other.mtl"} ||
		objBytes != "# mtllib ignored.mtl\r\n\tmtllib ../materials/共有 Material.mtl\r\nusemtl body\nmtllib ../other.mtl") {
		return false;
	}
	// 二つ目の参照で失敗しても元の文書を維持する
	for (bool throws : {false, true}) {
		objBytes = obj;
		uint32_t count = 0;
		if (ModelDocumentReferences::Rewrite(
				"model.obj", objBytes,
				[&](std::string& reference) {
					reference = "changed";
					if (++count == 2) {
						if (throws) {
							throw std::runtime_error("OBJ reference fixture");
						}
						return false;
					}
					return true;
				},
				objDiagnostic) ||
			objDiagnostic.empty() || objBytes != obj) {
			return false;
		}
	}
	const std::string original =
		R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin"},{"uri":"data:application/octet-stream;base64,AAAA"}],"images":[{"uri":"画像.png"},{"bufferView":0}],"extras":{"uri":"untouched.bin"}})";
	std::string diagnostic;
	// 別フォルダーへの配置でもBufferと画像の共有先を維持する
	std::string rebased = original;
	const std::filesystem::path source("C:/ModelReferences/Source/model.gltf");
	const std::filesystem::path target("C:/ModelReferences/Target/copy.gltf");
	if (!ModelDocumentReferences::Rebase(source, target, rebased, diagnostic)) {
		return false;
	}
	const auto references = nlohmann::json::parse(rebased);
	for (const auto& reference : {references["buffers"][0]["uri"], references["images"][0]["uri"]}) {
		const auto resolved = (target.parent_path() / Algorithm::PathFromUTF8(reference.get<std::string>())).lexically_normal();
		if (resolved.parent_path() != source.parent_path()) {
			return false;
		}
	}
	if (references["buffers"][1]["uri"] != nlohmann::json::parse(original)["buffers"][1]["uri"] ||
		references["extras"]["uri"] != "untouched.bin") {
		return false;
	}
	// 同じフォルダーでの改名は元のbyte列を保つ
	rebased = original;
	if (!ModelDocumentReferences::Rebase(source, source.parent_path() / "copy.gltf", rebased, diagnostic) ||
		rebased != original) {
		return false;
	}
	std::vector<std::string> calls;
	const auto rewrite = [&](std::string& uri) {
		calls.push_back(uri);
		uri = "dependencies/" + uri;
		return true;
	};
	std::string bytes = original;
	if (!GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic) || !diagnostic.empty() ||
		calls != std::vector<std::string>{"geometry.bin", "画像.png"}) {
		return false;
	}
	const auto document = nlohmann::json::parse(bytes);
	if (document["buffers"][0]["uri"] != "dependencies/geometry.bin" ||
		document["images"][0]["uri"] != "dependencies/画像.png" || document["extras"]["uri"] != "untouched.bin") {
		return false;
	}
	// 変更がなければ字下げもbyte列も維持する
	bytes = original;
	if (!GLTFDocumentReferences::Rewrite(bytes, [](std::string&) { return true; }, diagnostic) || bytes != original) {
		return false;
	}
	for (bool throws : {false, true}) {
		bytes = original;
		uint32_t count = 0;
		const auto fail = [&](std::string& uri) {
			uri = "changed";
			if (++count == 2) {
				if (throws) {
					throw std::runtime_error("reference fixture");
				}
				return false;
			}
			return true;
		};
		if (GLTFDocumentReferences::Rewrite(bytes, fail, diagnostic) || diagnostic.empty() || bytes != original) {
			return false;
		}
	}
	// JSONの長さが変わってもBinaryの零byteを保持する
	const std::string payload("a\0bc\0def", 8);
	const auto binary = MakeBinary(original, payload);
	bytes = binary;
	calls.clear();
	if (!GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic) || !bytes.ends_with(payload) ||
		calls != std::vector<std::string>{"geometry.bin", "画像.png"}) {
		return false;
	}
	uint32_t length = 0, jsonLength = 0;
	std::memcpy(&length, bytes.data() + 8, sizeof(length));
	std::memcpy(&jsonLength, bytes.data() + 12, sizeof(jsonLength));
	if (length != bytes.size() || jsonLength % 4 != 0 ||
		nlohmann::json::parse(bytes.substr(20, jsonLength))["images"][0]["uri"] != "dependencies/画像.png") {
		return false;
	}
	// HeaderとChunkが途中で切れた文書は更新しない
	for (size_t size : {size_t{4}, size_t{12}, size_t{16}, binary.size() - 1}) {
		bytes = binary.substr(0, size);
		const auto before = bytes;
		if (GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic) || diagnostic.empty() || bytes != before) {
			return false;
		}
	}
	// JSON順序とChunk範囲の破損をBinary全体で検査する
	uint32_t originalJSONLength = 0;
	std::memcpy(&originalJSONLength, binary.data() + 12, sizeof(originalJSONLength));
	for (const auto [offset, value] : std::vector<std::pair<size_t, uint32_t>>{
			 {12, 0xFFFFFFFC}, {12, originalJSONLength - 1}, {16, 0x004E4942}, {24 + originalJSONLength, 0x4E4F534A}}) {
		bytes = binary;
		std::memcpy(bytes.data() + offset, &value, sizeof(value));
		const auto before = bytes;
		if (GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic) || diagnostic.empty() || bytes != before) {
			return false;
		}
	}
	for (const char* malformed : {R"({"buffers":false})", R"({"images":[false]})", R"({"images":[{"uri":42}]})"}) {
		bytes = malformed;
		if (GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic) || diagnostic.empty() || bytes != malformed) {
			return false;
		}
	}
	// glTF1の名前付き項目も対象にする
	bytes = R"({"buffers":{"mesh":{"uri":"mesh.bin"}},"images":{"color":{"uri":"color.png"}}})";
	return GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic) &&
		   nlohmann::json::parse(bytes)["buffers"]["mesh"]["uri"] == "dependencies/mesh.bin";
}

bool NEMTests::CheckProjectModelReferenceCopy() {

	using namespace Engine;
	// 子ノードも所有ポインタではなくconst参照で取得する
	using Children = decltype(std::declval<const ProjectDirectoryNode&>().GetChildren());
	static_assert(std::ranges::forward_range<Children> && std::ranges::sized_range<Children>);
	static_assert(!kExposesStorage<Children>);
	using OwnedChildren = std::vector<std::unique_ptr<ProjectDirectoryNode>>;
	static_assert(!std::is_constructible_v<Children, OwnedChildren&&>);
	static_assert(!std::is_constructible_v<Children, const OwnedChildren&&>);
	// アドレス演算に依存せず、前後の位置を列挙する
	std::vector<std::unique_ptr<AddressProbe>> probes;
	for (int value : {17, 29}) {
		auto probe = std::make_unique<AddressProbe>();
		probe->value = value;
		probes.push_back(std::move(probe));
	}
	const ReadOnlyPointeeRange probeRange(probes);
	auto position = probeRange.begin();
	if (probeRange.size() != 2 || position.operator->() != probes[0].get() || (position++)->value != 17 ||
		position->value != 29 || ++position != probeRange.end()) {
		return false;
	}
	static_assert(
		std::is_same_v<std::ranges::range_reference_t<decltype(std::declval<const ProjectDirectoryNode&>().GetChildren())>,
			const ProjectDirectoryNode&>);
	TestDirectory assets("ProjectModelReferences", RuntimePaths::GetGameAssetsRoot());
	ProjectAssetEntry entry;
	nlohmann::json data;
	// glTFの単体複製は共有Bufferを改名しない
	const auto model = assets.GetPath() / "shared.gltf";
	const auto sharedBuffer = assets.GetPath() / "shared.bin";
	const nlohmann::json modelData = {
		{"buffers", {{{"uri", "shared.bin"}}}}, {"images", {{{"uri", "画像.png"}}}}, {"extras", {{"uri", "untouched.bin"}}}};
	if (!JsonFile::Save(model, modelData) || !StorageFileUtility::WriteBytes(sharedBuffer, "shared geometry")) {
		return false;
	}
	entry.assetPath = RuntimePaths::ToAssetPath(model);
	entry.type = AssetType::Mesh;
	entry.sidecarFiles = {"shared.bin"};
	const auto modelCopy = ProjectAssetCopyUtility::DuplicateAsset(entry, {});
	if (!modelCopy.success || !JsonFile::TryLoad(modelCopy.fullPath, data) || data != modelData ||
		std::filesystem::exists(modelCopy.fullPath.parent_path() / (modelCopy.fullPath.stem().wstring() + L".bin"))) {
		return false;
	}
	// 別フォルダーへのコピーも元の共有先を参照する
	const auto copiedFolder = assets.GetPath() / "Models";
	std::filesystem::create_directory(copiedFolder);
	const auto modelVirtualDirectory = "GameAssets/" + assets.GetPath().filename().string() + "/Models";
	const auto relocated = ProjectAssetCopyUtility::CopyAsset(entry, ProjectAssetSource::Game, modelVirtualDirectory, {});
	if (!relocated.success || !JsonFile::TryLoad(relocated.fullPath, data) ||
		(relocated.fullPath.parent_path() / Algorithm::PathFromUTF8(data["buffers"][0]["uri"].get<std::string>()))
				.lexically_normal() != sharedBuffer.lexically_normal() ||
		std::filesystem::exists(copiedFolder / "shared.bin")) {
		return false;
	}

	// 改名と移動は共有Bufferとmetaの内容を維持する
	if (!StorageFileUtility::WriteBytes(std::filesystem::path(model.wstring() + L".meta"), "model identity")) {
		return false;
	}
	const auto renamed = ProjectAssetMoveUtility::RenameAsset(entry, "renamed");
	if (!renamed.success || !std::filesystem::exists(sharedBuffer) || !JsonFile::TryLoad(renamed.fullPath, data) ||
		data != modelData) {
		return false;
	}
	entry.assetPath = renamed.assetPath;
	const auto moved = ProjectAssetMoveUtility::MoveAsset(entry, ProjectAssetSource::Game, modelVirtualDirectory);
	if (!moved.success || !std::filesystem::exists(sharedBuffer) || std::filesystem::exists(renamed.fullPath) ||
		!JsonFile::TryLoad(moved.fullPath, data) ||
		(moved.fullPath.parent_path() / Algorithm::PathFromUTF8(data["buffers"][0]["uri"].get<std::string>()))
				.lexically_normal() != sharedBuffer.lexically_normal() ||
		StorageFileUtility::ReadVerifiedBytes(std::filesystem::path(moved.fullPath.wstring() + L".meta"),
			StorageFileUtility::FileRevision(std::filesystem::path(moved.fullPath.wstring() + L".meta"))) != "model identity") {
		return false;
	}
	// 共有BinaryをProjectの独立したファイルとして表示する
	const auto indexedModel = assets.GetPath() / "indexed.gltf";
	const auto indexedBuffer = assets.GetPath() / "indexed.bin";
	if (!JsonFile::Save(indexedModel, modelData) || !StorageFileUtility::WriteBytes(indexedBuffer, "shared buffer")) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	const auto modelAsset = RuntimePaths::ToAssetPath(indexedModel);
	const auto bufferAsset = RuntimePaths::ToAssetPath(indexedBuffer);
	if (!database.ImportOrGet(modelAsset, AssetType::Mesh) || !database.ImportOrGet(bufferAsset, AssetType::DefaultAsset)) {
		return false;
	}
	ProjectAssetIndex index;
	if (!index.Rebuild(database, ProjectAssetSource::Game) || index.GetSource() != ProjectAssetSource::Game) {
		return false;
	}
	const auto* indexedEntry = index.FindAssetByPath(modelAsset);
	if (!indexedEntry || !indexedEntry->sidecarFiles.empty() || !index.FindAssetByPath(bufferAsset)) {
		return false;
	}
	// 古い一覧の付随指定が残っていても共有Binaryを削除しない
	ProjectAssetEntry deletion = *indexedEntry;
	// ソースの切替は一覧と所属を一緒に更新する
	if (!index.Rebuild(database, ProjectAssetSource::Engine) || index.GetSource() != ProjectAssetSource::Engine ||
		index.FindAssetByPath(modelAsset) || !index.Rebuild(database, ProjectAssetSource::Game) ||
		index.GetSource() != ProjectAssetSource::Game || !index.FindAssetByPath(modelAsset)) {
		return false;
	}
	deletion.sidecarFiles = {"indexed.bin"};
	SceneAssetStorage storage;
	std::string deletionDiagnostic;
	const auto deletionPaths = ProjectAssetDocumentPatch::BuildAssetSidecarPaths(deletion, indexedModel);
	if (!storage.Delete(indexedModel, database, deletionDiagnostic, deletionPaths) || std::filesystem::exists(indexedModel) ||
		std::filesystem::exists(std::filesystem::path(indexedModel.wstring() + L".meta")) ||
		!std::filesystem::exists(indexedBuffer)) {
		return false;
	}
	return CheckProjectOBJReferenceOperations();
}

bool NEMTests::CheckProjectOBJReferenceOperations() {

	using namespace Engine;
	const auto fail = [](const char* message) {
		std::cerr << "OBJ shared reference: " << message << '\n';
		return false;
	};
	TestDirectory assets("ProjectOBJReferences", RuntimePaths::GetGameAssetsRoot());
	const auto model = assets.GetPath() / "shared.obj";
	const auto material = assets.GetPath() / "shared.mtl";
	const std::string geometry = "mtllib shared.mtl\nusemtl body\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
	const auto color = assets.GetPath() / "shared_color.png";
	const std::string shading = "newmtl body\nKd 0.3 0.5 0.7\nmap_Kd -s 1 1 1 shared_color.png\n";
	if (!StorageFileUtility::WriteBytes(model, geometry) || !StorageFileUtility::WriteBytes(material, shading) ||
		!StorageFileUtility::WriteBytes(color, "physical image fixture")) {
		return fail("fixture publication");
	}
	const auto read = [](const auto& path) {
		return StorageFileUtility::ReadVerifiedBytes(path, StorageFileUtility::FileRevision(path));
	};
	ProjectAssetEntry entry;
	entry.type = AssetType::Mesh;
	entry.assetPath = RuntimePaths::ToAssetPath(model);
	entry.sidecarFiles = {"shared.mtl"};
	// 同じMaterialを参照する複製を作り、MTLは増やさない
	const auto duplicate = ProjectAssetCopyUtility::DuplicateAsset(entry, {});
	if (!duplicate.success || read(duplicate.fullPath) != geometry ||
		std::filesystem::exists(duplicate.fullPath.parent_path() / (duplicate.fullPath.stem().wstring() + L".mtl"))) {
		return fail("duplicate Material ownership");
	}
	const auto directory = assets.GetPath() / "Models";
	std::filesystem::create_directory(directory);
	const auto virtualDirectory = "GameAssets/" + assets.GetPath().filename().string() + "/Models";
	const auto copied = ProjectAssetCopyUtility::CopyAsset(entry, ProjectAssetSource::Game, virtualDirectory, {});
	if (!copied.success || !read(copied.fullPath).starts_with("mtllib ../shared.mtl\n") ||
		std::filesystem::exists(directory / "shared.mtl")) {
		return fail("copy relocation");
	}
	// 改名と移動後も通常の読込が元のMTLを使う
	const auto renamed = ProjectAssetMoveUtility::RenameAsset(entry, "renamed");
	if (!renamed.success || read(renamed.fullPath) != geometry || read(material) != shading) {
		return fail("rename shared Material");
	}
	entry.assetPath = renamed.assetPath;
	const auto moved = ProjectAssetMoveUtility::MoveAsset(entry, ProjectAssetSource::Game, virtualDirectory);
	std::vector<std::filesystem::path> dependencies;
	std::string diagnostic;
	if (!moved.success || !ModelFileDependencyCollector::Collect(moved.fullPath, dependencies, diagnostic) ||
		std::find(dependencies.begin(), dependencies.end(), std::filesystem::absolute(material).lexically_normal()) ==
			dependencies.end() ||
		std::find(dependencies.begin(), dependencies.end(), std::filesystem::absolute(color).lexically_normal()) ==
			dependencies.end() ||
		read(material) != shading) {
		return fail("move physical Material and image");
	}
	// 同名MTLも独立したAssetとして表示し削除対象に含めない
	AssetDatabase database;
	database.Init();
	if (!StorageFileUtility::WriteBytes(model, geometry) ||
		!database.ImportOrGet(RuntimePaths::ToAssetPath(model), AssetType::Mesh) ||
		!database.ImportOrGet(duplicate.assetPath, AssetType::Mesh) ||
		!database.ImportOrGet(RuntimePaths::ToAssetPath(material), AssetType::DefaultAsset)) {
		return fail("index fixtures");
	}
	ProjectAssetIndex index;
	if (!index.Rebuild(database, ProjectAssetSource::Game) || !index.FindAssetByPath(RuntimePaths::ToAssetPath(material))) {
		return fail("same-stem Material visibility");
	}
	SceneAssetStorage storage;
	entry.assetPath = duplicate.assetPath;
	const auto sidecars = ProjectAssetDocumentPatch::BuildAssetSidecarPaths(entry, duplicate.fullPath);
	if (!storage.Delete(duplicate.fullPath, database, diagnostic, sidecars) || read(material) != shading) {
		return fail("delete shared Material ownership");
	}
	// 宣言先がなく同名MTLを補完する場合は共有先を明示する
	const auto fallback = assets.GetPath() / "fallback.obj";
	const auto fallbackMaterial = assets.GetPath() / "fallback.mtl";
	if (!StorageFileUtility::WriteBytes(fallback, "mtllib absent.mtl\n" + geometry.substr(geometry.find('\n') + 1)) ||
		!StorageFileUtility::WriteBytes(fallbackMaterial, shading)) {
		return fail("fallback fixtures");
	}
	entry.assetPath = RuntimePaths::ToAssetPath(fallback);
	const auto fallbackCopy = ProjectAssetCopyUtility::DuplicateAsset(entry, {});
	if (!fallbackCopy.success || !read(fallbackCopy.fullPath).starts_with("mtllib fallback.mtl\n")) {
		return fail("fallback normalization");
	}
	// 補完画像の基準が曖昧な文書は公開前に拒否する
	std::string ambiguous = "mtllib missing/library.mtl\n";
	const auto original = ambiguous;
	return !ModelDocumentReferences::Rebase(fallback, directory / "fallback.obj", ambiguous, diagnostic) &&
		   ambiguous == original && !diagnostic.empty();
}
