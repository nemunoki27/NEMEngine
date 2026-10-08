#include "ModelFileURITests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/GLTFFileReference.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileIOSystem.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Assets/Project/ProjectAssetCopyUtility.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>

// c++
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {

	// 予約文字を復号し、復号後の%をもう一度解釈しない
	bool CheckFileReferenceEncoding() {

		using namespace Engine;
		// 未指定の入力は文字列へ変換する前に返す
		ModelFileIOSystem fileSystem("unused.gltf");
		if (fileSystem.Exists(nullptr) || fileSystem.Open(nullptr, "rb") || fileSystem.Open("unused.gltf", nullptr)) {
			return false;
		}
		for (const char* name : {"folder/geometry %20.bin", "images/日本語%20#.png", "../画像.png"}) {
			const auto path = Algorithm::PathFromUTF8(name);
			if (GLTFFileReference::Decode(GLTFFileReference::Encode(path)) != path) {
				return false;
			}
		}
		if (GLTFFileReference::Decode("%E6%97%A5%E6%9C%AC%E8%AA%9E.png") !=
			Algorithm::PathFromUTF8("日本語.png")) {
			return false;
		}
		for (const char* invalid : {"broken%", "broken%GG", "nul%00.png", "https://server/image.png",
			"//server/image.png", "image.png?query", "image.png#fragment"}) {
			bool rejected = false;
			try {
				GLTFFileReference::Decode(invalid);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			if (!rejected) {
				return false;
			}
		}
		// UTF-8の検証は共通の文字列変換に任せる
		try {
			GLTFFileReference::Decode("%FF.png");
			return false;
		} catch (const std::runtime_error&) {
		}
		// 参照が不正なら途中の文書更新を公開しない
		std::string bytes = R"({"buffers":[{"uri":"broken%GG.bin"}]})";
		const auto original = bytes;
		std::string diagnostic;
		return !ModelDocumentReferences::Rebase("C:/URITest/source.gltf", "C:/URITest/copy/target.gltf", bytes, diagnostic) &&
			bytes == original && !diagnostic.empty();
	}

	// 元の配置を消しても通常Importerから頂点を取得する
	bool CheckImportedGeometry(const std::filesystem::path& model) {

		const auto imported = Engine::MeshFileImporter::ImportFile({}, model, {}, {}, false);
		return imported.vertices.size() == 3 && imported.indices.size() == 3 && imported.subMeshes.size() == 1;
	}

	// OBJのファイル名はURIとして復号しない
	bool CheckOBJReferences(const std::filesystem::path& root) {

		using namespace Engine;
		const auto model = root / "literal%20.obj";
		const auto material = root / "literal%20.mtl";
		const auto image = root / "literal%20.png";
		if (!StorageFileUtility::WriteBytes(model,
			"mtllib literal%20.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n") ||
			!StorageFileUtility::WriteBytes(material, "newmtl Surface\nmap_Kd literal%20.png\n") ||
			!StorageFileUtility::WriteBytes(image, "literal")) {
			return false;
		}
		ModelFileDependencyCollector::ModelDependencies dependencies;
		std::string diagnostic;
		if (!ModelFileDependencyCollector::Collect(model, dependencies, diagnostic) ||
			!CheckImportedGeometry(model)) {
			return false;
		}
		return std::find(dependencies.files.begin(), dependencies.files.end(), material) != dependencies.files.end() &&
			std::find(dependencies.files.begin(), dependencies.files.end(), image) != dependencies.files.end();
	}
}

bool NEMTests::CheckModelFileURIReferences() {

	using namespace Engine;
	if (!CheckFileReferenceEncoding()) {
		return false;
	}
	TestDirectory external("ModelFileURI%20");
	TestDirectory assets("ModelFileURIAssets", RuntimePaths::GetGameAssetsRoot());
	const auto source = external.GetPath() / Algorithm::PathFromUTF8("日本語 # %20");
	const auto target = assets.GetPath();
	if (!CheckOBJReferences(external.GetPath() / "obj%20")) {
		return false;
	}
	const auto model = source / "model %20.gltf";
	const auto buffer = source / "buffers/geometry %20.bin";
	const auto image = source / Algorithm::PathFromUTF8("images/色 %20#.png");
	const auto unusedImage = source / Algorithm::PathFromUTF8("images/未使用 %20#.png");
	const std::array<float, 9> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0};
	const std::string geometry(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
	auto document = nlohmann::json::parse(R"({"asset":{"version":"2.0"},"scene":0,
"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
"buffers":[{"uri":"unused","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
"images":[{"uri":"unused"}],"textures":[{"source":0}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}]})");
	document["buffers"][0]["uri"] = GLTFFileReference::Encode(buffer.lexically_relative(source));
	document["images"][0]["uri"] = GLTFFileReference::Encode(image.lexically_relative(source));
	document["images"].push_back({{"uri", GLTFFileReference::Encode(unusedImage.lexically_relative(source))}});
	if (!StorageFileUtility::WriteBytes(buffer, geometry) || !StorageFileUtility::WriteBytes(image, "image bytes") ||
		!StorageFileUtility::WriteBytes(unusedImage, "unused image") ||
		!JsonFile::SaveCanonical(model, document, 2)) {
		return false;
	}
	// BufferとMaterial画像が同じ実ファイルへ解決される
	ModelFileDependencyCollector::ModelDependencies dependencies;
	std::string diagnostic;
	if (!ModelFileDependencyCollector::Collect(model, dependencies, diagnostic) || dependencies.files.size() != 3 ||
		dependencies.materialTextures.size() != 2 || dependencies.materialTextures[0] != std::vector{image} ||
		!dependencies.materialTextures[1].empty() ||
		!CheckImportedGeometry(model)) {
		std::cerr << "Model URI source failed: " << diagnostic << '\n';
		return false;
	}
	// 別フォルダーへ移した文書も参照先を維持する
	auto rebased = document.dump();
	const auto movedModel = source / "moved/copy.gltf";
	if (!ModelDocumentReferences::Rebase(model, movedModel, rebased, diagnostic) ||
		!StorageFileUtility::WriteBytes(movedModel, rebased) || !CheckImportedGeometry(movedModel)) {
		return false;
	}
	const auto virtualDirectory = "GameAssets/" + target.filename().string();
	const auto imported = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, model);
	if (!imported.success || imported.fullPath != target / "model %20/model %20.gltf") {
		std::cerr << "Model URI import failed: " << imported.message << '\n';
		return false;
	}
	nlohmann::json copied;
	if (!JsonFile::TryLoad(imported.fullPath, copied)) {
		return false;
	}
	const auto bundle = imported.fullPath.parent_path();
	const auto copiedBuffer = bundle / GLTFFileReference::Decode(copied["buffers"][0]["uri"].get<std::string>());
	const auto copiedImage = bundle / GLTFFileReference::Decode(copied["images"][0]["uri"].get<std::string>());
	const auto copiedUnused = bundle / GLTFFileReference::Decode(copied["images"][1]["uri"].get<std::string>());
	if (StorageFileUtility::ReadVerifiedBytes(copiedBuffer, StorageFileUtility::FileRevision(copiedBuffer)) != geometry ||
		StorageFileUtility::ReadVerifiedBytes(copiedImage, StorageFileUtility::FileRevision(copiedImage)) != "image bytes" ||
		StorageFileUtility::ReadVerifiedBytes(copiedUnused, StorageFileUtility::FileRevision(copiedUnused)) != "unused image" ||
		!external.Remove() || !ModelFileDependencyCollector::Collect(imported.fullPath, dependencies, diagnostic) ||
		dependencies.files.size() != 3 || dependencies.materialTextures.size() != 2 ||
		!dependencies.materialTextures[1].empty() ||
		dependencies.materialTextures[0] != std::vector{copiedImage} || !CheckImportedGeometry(imported.fullPath)) {
		return false;
	}
	// Inspectorの解析も同じ外部Bufferを読み込む
	AssetDatabase database;
	if (!database.Init()) {
		return false;
	}
	const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(imported.fullPath), AssetType::Mesh);
	std::vector<MeshSubMeshLayoutItem> layout;
	if (!mesh || !MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) || layout.size() != 1) {
		return false;
	}
	// Projectのプレビューも同じ頂点範囲を取得する
	Vector3 min, max, center;
	float radius = 0.0f;
	if (!ModelPreviewUtility::ComputeBounds(database, mesh, min, max, center, radius) ||
		min.x != -1.0f || max.y != 1.0f || center != Vector3(-0.5f, 0.5f, 0.0f) || radius <= 0.0f) {
		return false;
	}
	return std::all_of(dependencies.files.begin(), dependencies.files.end(), [&](const auto& file) {
		return StorageFileUtility::IsInside(file, bundle);
	});
}
