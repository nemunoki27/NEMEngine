#include "TestContracts.h"
#include "TestFixtures.h"
#include "ModelFileURITests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetCopyUtility.h>
#include <Engine/Editor/Assets/Project/ProjectDirectoryCopyTransaction.h>
#include <Engine/Editor/Assets/Project/ProjectModelImportPlan.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <iostream>

namespace {

	// 一時保存名を含む長いパスでも所有と内容を照合する
	bool CheckLongFileCopy(const std::filesystem::path& root) {

		using namespace Engine;
		const auto directory = root / std::string(90, 'a') / std::string(90, 'b');
		const auto source = directory / "source.bin";
		const auto target = directory / "target.bin";
		if (target.native().size() <= 260 || !StorageFileUtility::WriteBytes(source, "long-path")) {
			return false;
		}
		StorageFileUtility::FileIdentity identity;
		std::string revision;
		if (!StorageFileUtility::CopyFileWithoutReplacement(source, target, identity, revision) ||
			StorageFileUtility::ReadVerifiedBytes(target, revision) != "long-path") {
			return false;
		}
		std::error_code error;
		StorageFileUtility::FileIdentity copied;
		if (!StorageFileUtility::ReadIdentity(target, copied, error) || copied != identity ||
			!StorageFileUtility::RemoveIfRevision(target, revision, identity, error) ||
			!StorageFileUtility::RemoveIfRevision(source, revision, error)) {
			return false;
		}
		return std::filesystem::remove(Algorithm::ToFileSystemPath(directory)) &&
			std::filesystem::remove(Algorithm::ToFileSystemPath(directory.parent_path()));
	}

	// OBJの補完MTLと画像を用意する
	bool PrepareOBJ(const std::filesystem::path& root) {

		using namespace Engine;
		return StorageFileUtility::WriteBytes(root / "surface.obj",
				   "mtllib missing.mtl\r\no Surface\r\nv 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\nusemtl Surface\r\nf 1 2 3\r\n") &&
			StorageFileUtility::WriteBytes(root / "surface.mtl",
				"newmtl Surface\r\nKd 0.4 0.5 0.6\r\nmap_Kd -s 1 1 1 -clamp on bundle_surface_diffuse.png\r\n") &&
			StorageFileUtility::WriteBytes(root / "bundle_surface_diffuse.png", "diffuse") &&
			StorageFileUtility::WriteBytes(root / "bundle_surface_normal.png", "normal");
	}

	// 画像とオプション以外のMTL文字を維持する
	bool CheckMaterialRewrite() {

		const std::string original = "\xEF\xBB\xBFnewmtl Surface\r\nKd 1 1 1\r\n"
									 "\tmap_Kd -o -1 0 0 -mm 0 1 -clamp on folder/共有 color.png\r\n"
									 "norm -bm 0.5 normal.png\n# map_Kd ignored.png\nrefl ignored.png";
		auto bytes = original;
		std::string diagnostic;
		std::vector<std::string> references;
		if (!Engine::ModelDocumentReferences::RewriteMaterial(
				bytes,
				[&](std::string& reference) {
					references.push_back(reference);
					reference = "../" + reference;
					return true;
				},
				diagnostic) ||
			references != std::vector<std::string>{"folder/共有 color.png", "normal.png"} ||
			bytes !=
				"\xEF\xBB\xBFnewmtl Surface\r\nKd 1 1 1\r\n"
				"\tmap_Kd -o -1 0 0 -mm 0 1 -clamp on ../folder/共有 color.png\r\n"
				"norm -bm 0.5 ../normal.png\n# map_Kd ignored.png\nrefl ignored.png") {
			return false;
		}
		// 二つ目の参照失敗では途中編集を公開しない
		bytes = original;
		size_t count = 0;
		return !Engine::ModelDocumentReferences::RewriteMaterial(
				   bytes,
				   [&](std::string& reference) {
					   reference = "changed";
					   return ++count != 2;
				   },
				   diagnostic) &&
			bytes == original && !diagnostic.empty();
	}

	// glTFのBufferと未使用画像も専用フォルダーへ取り込む
	bool CheckGLTFImport(
		const std::filesystem::path& root, const std::filesystem::path& assets, const std::string& virtualDirectory) {

		using namespace Engine;
		std::filesystem::create_directory(root / "buffers");
		const std::array<float, 9> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0};
		const std::string buffer(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
		auto document = nlohmann::json::parse(R"({"asset":{"version":"2.0"},"scene":0,
"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
"buffers":[{"uri":"buffers/geometry.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
"images":[{"uri":"unused.png"}],"extras":{"uri":"unrelated.bin"}})");
		// 別フォルダーの同名画像を別の参照として維持する
		document["images"].push_back({{"uri", "a/shared.png"}});
		document["images"].push_back({{"uri", "b/shared.png"}});
		if (!StorageFileUtility::WriteBytes(root / "buffers/geometry.bin", buffer) ||
			!StorageFileUtility::WriteBytes(root / "unused.png", "unused") ||
			!StorageFileUtility::WriteBytes(root / "a/shared.png", "first") ||
			!StorageFileUtility::WriteBytes(root / "b/shared.png", "second") ||
			!JsonFile::SaveCanonical(root / "mesh.gltf", document, 2)) {
			return false;
		}
		const auto imported =
			ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, root / "mesh.gltf");
		nlohmann::json copied;
		if (!imported.success || imported.fullPath != assets / "mesh/mesh.gltf" ||
			!JsonFile::TryLoad(imported.fullPath, copied) || copied["extras"] != document["extras"]) {
			std::cerr << "glTF import failed: " << imported.message << '\n';
			return false;
		}
		const auto copyRoot = imported.fullPath.parent_path();
		const auto bufferPath = copyRoot / copied["buffers"][0]["uri"].get<std::string>();
		const auto imagePath = copyRoot / copied["images"][0]["uri"].get<std::string>();
		if (StorageFileUtility::ReadVerifiedBytes(bufferPath, StorageFileUtility::FileRevision(bufferPath)) != buffer ||
			StorageFileUtility::ReadVerifiedBytes(imagePath, StorageFileUtility::FileRevision(imagePath)) != "unused") {
			return false;
		}
		for (size_t index = 1; index < 3; ++index) {
			const auto path = copyRoot / copied["images"][index]["uri"].get<std::string>();
			if (StorageFileUtility::ReadVerifiedBytes(path, StorageFileUtility::FileRevision(path)) !=
				(index == 1 ? "first" : "second")) {
				return false;
			}
		}

		// GLBの頂点Bufferを保ち、外部画像の参照をつなぎ直す
		auto binaryDocument = document;
		binaryDocument["buffers"][0].erase("uri");
		auto json = binaryDocument.dump();
		json.append((4 - json.size() % 4) % 4, ' ');
		const std::array<uint32_t, 5> header{0x46546C67, 2, static_cast<uint32_t>(28 + json.size() + buffer.size()),
			static_cast<uint32_t>(json.size()), 0x4E4F534A};
		std::string binary(reinterpret_cast<const char*>(header.data()), sizeof(header));
		binary += json;
		const std::array<uint32_t, 2> bufferHeader{static_cast<uint32_t>(buffer.size()), 0x004E4942};
		binary.append(reinterpret_cast<const char*>(bufferHeader.data()), sizeof(bufferHeader));
		binary += buffer;
		if (!StorageFileUtility::WriteBytes(root / "binary.glb", binary)) {
			return false;
		}
		const auto glb = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, root / "binary.glb");
		if (!glb.success) {
			std::cerr << "GLB import failed: " << glb.message << '\n';
			return false;
		}
		auto glbBytes = StorageFileUtility::ReadVerifiedBytes(glb.fullPath, StorageFileUtility::FileRevision(glb.fullPath));
		std::string diagnostic;
		size_t references = 0;
		if (!ModelDocumentReferences::Rewrite(glb.fullPath, glbBytes, [&](std::string& reference) {
				++references;
				const auto path = glb.fullPath.parent_path() / reference;
				return StorageFileUtility::IsInside(path, glb.fullPath.parent_path()) && std::filesystem::is_regular_file(path);
			}, diagnostic) || references != 3) {
			return false;
		}

		// フォルダー外のBufferと画像を取り込み、既存の同名フォルダーを残す
		std::filesystem::create_directories(root / "pack/mesh");
		document["buffers"][0]["uri"] = "../buffers/geometry.bin";
		for (auto& image : document["images"]) {
			image["uri"] = "../" + image["uri"].get<std::string>();
		}
		if (!JsonFile::SaveCanonical(root / "pack/mesh.gltf", document, 2) ||
			!StorageFileUtility::WriteBytes(root / "pack/mesh/keep.txt", "keep")) {
			return false;
		}
		{
			NEMTests::TestFileReadLock lock(root / "unused.png", false);
			const auto failed = ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource::Game, virtualDirectory, root / "pack");
			if (failed.success || failed.message.empty() || std::filesystem::exists(assets / "pack")) {
				return false;
			}
		}
		const auto folder = ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource::Game, virtualDirectory, root / "pack");
		if (!folder.success || !folder.isDirectory || folder.fullPath != assets / "pack" ||
			!std::filesystem::is_regular_file(folder.fullPath / "mesh/keep.txt") ||
			!JsonFile::TryLoad(folder.fullPath / "mesh 1/mesh.gltf", copied)) {
			std::cerr << "Model folder import failed: " << folder.message << '\n';
			return false;
		}
		for (const auto& image : copied["images"]) {
			const auto path = folder.fullPath / "mesh 1" / image["uri"].get<std::string>();
			if (!StorageFileUtility::IsInside(path, folder.fullPath) || !std::filesystem::is_regular_file(path)) {
				return false;
			}
		}
		return true;
	}
}

bool NEMTests::TestModelImportBundle() {

	using namespace Engine;
	if (!CheckMaterialRewrite()) {
		std::cerr << "Material reference rewrite failed\n";
		return false;
	}
	TestDirectory external("ModelImportSource");
	if (!CheckModelFileURIReferences()) {
		return false;
	}
	TestDirectory assets("ModelImportBundle", RuntimePaths::GetGameAssetsRoot());
	const auto source = external.GetPath();
	const auto target = assets.GetPath();
	const auto virtualDirectory = "GameAssets/" + target.filename().string();
	if (!CheckLongFileCopy(target) || !PrepareOBJ(source)) {
		return false;
	}
	// 読込失敗で専用フォルダーと準備中のファイルを残さない
	{
		TestFileReadLock lock(source / "bundle_surface_diffuse.png", false);
		const auto failed =
			ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, source / "surface.obj");
		if (failed.success || failed.message.empty() || !std::filesystem::is_empty(target)) {
			return false;
		}
	}
	// 通常読込のMTL補完とNormal補完を維持する
	const auto imported =
		ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, source / "surface.obj");
	if (!imported.success || imported.isDirectory || imported.fullPath != target / "surface/surface.obj" ||
		imported.assetPath.empty()) {
		std::cerr << "OBJ bundle import failed: " << imported.message << '\n';
		return false;
	}
	std::vector<std::filesystem::path> dependencies;
	std::string diagnostic;
	if (!ModelFileDependencyCollector::Collect(imported.fullPath, dependencies, diagnostic) || dependencies.size() != 4) {
		return false;
	}
	for (const auto& file : dependencies) {
		if (!StorageFileUtility::IsInside(file, target / "surface")) {
			return false;
		}
	}
	// 同じモデルを取り込んでも前回のフォルダーを上書きしない
	const auto repeated =
		ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, source / "surface.obj");
	if (!repeated.success || repeated.fullPath != target / "surface 1/surface.obj" ||
		!CheckGLTFImport(source, target, virtualDirectory)) {
		return false;
	}
	// 準備後の参照ファイル変更を公開前に拒否する
	ProjectModelImportPlan plan;
	if (!plan.Prepare(source / "surface.obj", diagnostic) ||
		!StorageFileUtility::WriteBytes(source / "bundle_surface_diffuse.png", "changed")) {
		return false;
	}
	{
		ProjectDirectoryCopyTransaction transaction(target / "changed");
		if (!transaction.Begin(diagnostic) || plan.Stage(transaction, diagnostic) || diagnostic.empty()) {
			return false;
		}
	}
	if (std::filesystem::exists(target / "changed")) {
		return false;
	}
	// 元フォルダーを除去しても取り込み先だけで読み込める
	if (!external.Remove() || !ModelFileDependencyCollector::Collect(imported.fullPath, dependencies, diagnostic) ||
		dependencies.size() != 4) {
		return false;
	}
	if (!ModelFileDependencyCollector::Collect(target / "pack/mesh 1/mesh.gltf", dependencies, diagnostic) ||
		dependencies.size() != 2) {
		return false;
	}
	for (const auto& file : dependencies) {
		if (!StorageFileUtility::IsInside(file, target / "pack")) {
			return false;
		}
	}
	for (const auto& entry : std::filesystem::directory_iterator(target)) {
		if (entry.path().filename().wstring().starts_with(L".nem-copy-")) {
			return false;
		}
	}
	return true;
}
