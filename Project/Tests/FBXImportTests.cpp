#include "FBXImportTests.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include <Engine/Editor/Assets/Project/ProjectAssetCopyUtility.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/FBXDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Textures/TextureAlphaAnalysis.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cmath>
#include <iostream>

// directX
#include <DirectXTex.h>

// assimp
#include <assimp/material.h>

namespace {

	// 有限の頂点と有効な三角形とMeshletが揃うことを確認する
	bool CheckGeometry(const Engine::ImportedMeshAsset& mesh) {

		if (mesh.vertices.empty() || mesh.indices.empty() || mesh.subMeshes.empty() || mesh.meshlets.empty() ||
			mesh.indices.size() % 3 != 0 || mesh.lods[0].indexCount != mesh.indices.size()) {
			return false;
		}
		return std::ranges::all_of(mesh.indices, [&](uint32_t index) { return index < mesh.vertices.size(); }) &&
			std::ranges::all_of(mesh.vertices, [](const auto& vertex) {
				return std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
					std::isfinite(vertex.position.z) && std::isfinite(vertex.normal.x) &&
					std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
			});
	}
	// 元のTexture配置に頼らず取り込み後のMaterialを解決する
	bool CheckTextureImport(const std::filesystem::path& source, const std::filesystem::path& target) {

		using namespace Engine;
		const auto model = source / "textured.FBX";
		if (!NEMTests::PrepareFBXTextures(source)) {
			return false;
		}
		const auto virtualDirectory = "GameAssets/" + target.filename().string();
		const auto imported = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, model);
		if (!imported.success || imported.fullPath != target / "textured/textured.FBX") {
			std::cerr << "FBX texture import failed: " << imported.message << '\n';
			return false;
		}
		std::vector<std::filesystem::path> files;
		std::string diagnostic;
		if (!ModelFileDependencyCollector::Collect(imported.fullPath, files, diagnostic) || files.size() != 8) {
			std::cerr << "FBX dependencies failed: " << diagnostic << " count=" << files.size() << '\n';
			return false;
		}
		for (const auto& file : files) {
			if (!StorageFileUtility::IsInside(file, imported.fullPath.parent_path())) return false;
		}
		// 読込不能な画像がある場合は公開先を残さない
		{
			NEMTests::TestFileReadLock lock(source / "Textures/albedo.png", false);
			const auto failed = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, model);
			if (failed.success || std::filesystem::exists(target / "textured 1")) return false;
		}
		const auto repeated = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, model);
		if (!repeated.success || repeated.fullPath != target / "textured 1/textured.FBX") return false;
		// 実際のバイナリFBXでも画像参照長の変更を通す
		const auto fixture = RuntimePaths::GetEngineProjectRoot() / "Externals/assimp/test/models/FBX/spider.fbx";
		auto binaryBytes = StorageFileUtility::ReadVerifiedBytes(fixture, StorageFileUtility::FileRevision(fixture));
		const auto image = source / "Textures/albedo.png";
		const auto pixels = StorageFileUtility::ReadVerifiedBytes(image, StorageFileUtility::FileRevision(image));
		const auto binaryModel = source / "texturedBinary.fbx";
		size_t references = 0;
		if (!FBXDocumentReferences::Rewrite(binaryBytes, [&](std::string& reference) {
				++references;
				auto name = Algorithm::PathFromUTF8(reference).filename();
				name.replace_extension(".png");
				reference = "Textures/" + Algorithm::PathToUTF8(name);
				return StorageFileUtility::WriteBytes(source / Algorithm::PathFromUTF8(reference), pixels);
			}, diagnostic) || references == 0 || !StorageFileUtility::WriteBytes(binaryModel, binaryBytes)) {
			std::cerr << "FBX fixture preparation failed: " << diagnostic << '\n';
			return false;
		}
		const auto binary = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game, virtualDirectory, binaryModel);
		if (!binary.success || !CheckGeometry(MeshFileImporter::ImportFile({}, binary.fullPath, {}, {})) ||
			!ModelFileDependencyCollector::Collect(binary.fullPath, files, diagnostic) || files.size() <= 1) {
			std::cerr << "Binary FBX texture import failed: " << binary.message << " " << diagnostic << '\n';
			return false;
		}
		// フォルダー単位でも各FBXと外部画像を一緒に取り込む
		const auto folder = ProjectAssetCopyUtility::ImportExternalDirectory(ProjectAssetSource::Game, virtualDirectory, source);
		if (!folder.success || !ModelFileDependencyCollector::Collect(
			folder.fullPath / "textured/textured.FBX", files, diagnostic) || files.size() != 8) {
			std::cerr << "FBX folder import failed: " << folder.message << " " << diagnostic << '\n';
			return false;
		}
		// 元の画像名を変え、同名検索による補完を使わせない
		std::filesystem::rename(source / "Textures", source / "RemovedTextures");
		if (!ModelFileDependencyCollector::Collect(binary.fullPath, files, diagnostic) || files.size() <= 1 ||
			!std::ranges::all_of(files, [&](const auto& file) {
				return StorageFileUtility::IsInside(file, binary.fullPath.parent_path());
			})) return false;
		AssetDatabase database;
		database.Init();
		const auto asset = database.ImportOrGet(imported.assetPath, AssetType::Mesh);
		const auto mesh = MeshFileImporter::ImportFile(asset, imported.fullPath, {}, {});
		std::vector<MeshSubMeshLayoutItem> layout;
		if (!asset || !CheckGeometry(mesh) || !MeshSubMeshAuthoring::TryBuildLayout(&database, asset, layout) ||
			layout.size() != mesh.subMeshes.size() || !layout.front().defaultTextureAssets.baseColorTexture ||
			!layout.front().defaultTextureAssets.normalTexture ||
			mesh.subMeshes.front().defaultTextures.baseColorTexturePath.empty()) {
			return false;
		}
		// 配置変更でも同じ画像を参照する
		std::filesystem::create_directory(target / "moved");
		auto bytes = StorageFileUtility::ReadVerifiedBytes(imported.fullPath, StorageFileUtility::FileRevision(imported.fullPath));
		const auto moved = target / "moved/textured.FBX";
		if (!ModelDocumentReferences::Rebase(imported.fullPath, moved, bytes, diagnostic) ||
			!StorageFileUtility::WriteBytes(moved, bytes) ||
			!ModelFileDependencyCollector::Collect(moved, files, diagnostic)) return false;
		return files.size() == 8;
	}
}

bool NEMTests::PrepareFBXTextures(const std::filesystem::path& directory) {

	using namespace Engine;
	const auto fixture = RuntimePaths::GetEngineProjectRoot() / "Externals/assimp/test/models/FBX/maxPbrMaterial_metalRough.fbx";
	if (!StorageFileUtility::WriteBytes(directory / "textured.FBX",
		StorageFileUtility::ReadVerifiedBytes(fixture, StorageFileUtility::FileRevision(fixture)))) return false;
	// 有効な1pixelのRGBA画像を外部ファイルとして用意する
	constexpr unsigned char image[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,
		8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,248,15,4,0,9,251,3,253,251,94,107,43,
		0,0,0,0,73,69,78,68,174,66,96,130};
	const std::string bytes(reinterpret_cast<const char*>(image), sizeof(image));
	for (const char* name : {"albedo", "metalness", "roughness", "occlusion", "normal", "emission", "opacity"}) {
		if (!StorageFileUtility::WriteBytes(directory / "Textures" / (std::string(name) + ".png"), bytes)) return false;
	}
	return true;
}

bool NEMTests::TestFBXImport() {

	using namespace Engine;
	if (!CheckFBXReferences() || !CheckFBXTransforms() || AssetTypeResolver::GuessByPath("mesh.FBX") != AssetType::Mesh ||
		!ModelDocumentReferences::IsDocumentPath("mesh.fbx")) return false;
	TestDirectory source("FBXExternal");
	// Alphaの有無ではなく不透明と切り抜きと半透明を分類
	DirectX::ScratchImage pixels;
	if (FAILED(pixels.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 1, 1, 1))) return false;
	const auto* image = pixels.GetImage(0, 0, 0);
	TextureAlphaAnalysis analysis;
	for (const auto content : {TextureAlphaContent::Opaque, TextureAlphaContent::Masked, TextureAlphaContent::Transparent}) {

		for (size_t x = 0; x < 8; ++x) {
			auto* pixel = image->pixels + x * 4;
			pixel[0] = pixel[1] = pixel[2] = 255;
			pixel[3] = content == TextureAlphaContent::Transparent ? 128 : content == TextureAlphaContent::Masked && x < 4 ? 0 : 255;
		}
		const auto path = source.GetPath() / (std::to_string(static_cast<int>(content)) + ".dds");
		if (FAILED(DirectX::SaveToDDSFile(*image, DirectX::DDS_FLAGS_NONE, path.c_str())) || analysis.Analyze(path) != content) return false;
		TextureAssetResolver resolver;
		resolver.Build(source.GetPath() / "alpha.fbx");
		aiMaterial material;
		aiString reference(path.filename().string());
		material.AddProperty(&reference, AI_MATKEY_TEXTURE_DIFFUSE(0));
		const auto surface = MeshImportUtility::ReadMaterialSurface(&material, &resolver);
		const auto expected = content == TextureAlphaContent::Opaque ? MaterialSurfaceMode::Auto :
			content == TextureAlphaContent::Masked ? MaterialSurfaceMode::Masked : MaterialSurfaceMode::Transparent;
		if (surface.surfaceMode != expected) return false;
		// glTFの明示OpaqueをTextureのAlphaで上書きしない
		aiString opaque("OPAQUE");
		material.AddProperty(&opaque, "$mat.gltf.alphaMode", 0, 0);
		if (MeshImportUtility::ReadMaterialSurface(&material, &resolver).surfaceMode != MaterialSurfaceMode::Opaque) return false;
	}
	TestDirectory assets("FBXImport", RuntimePaths::GetGameAssetsRoot());
	if (!CheckTextureImport(source.GetPath(), assets.GetPath())) return false;
	const auto fixtures = RuntimePaths::GetEngineProjectRoot() / "Externals/assimp/test/models/FBX";
	// 実際の32bitと64bitと圧縮配列と部品配置と骨を通す
	for (const char* name : {"box.fbx", "boxWithCompressedCTypeArray.FBX", "boxWithUncompressedCTypeArray.FBX",
		"cubes_with_mirroring_and_pivot.fbx", "animation_with_skeleton.fbx"}) {
		const auto imported = ProjectAssetCopyUtility::ImportExternalFile(ProjectAssetSource::Game,
			"GameAssets/" + assets.GetPath().filename().string(), fixtures / name);
		if (!imported.success) {
			std::cerr << "FBX import failed: " << name << " " << imported.message << '\n';
			return false;
		}
		const auto mesh = MeshFileImporter::ImportFile({}, imported.fullPath, {}, {});
		if (!CheckGeometry(mesh)) {
			std::cerr << "FBX geometry failed: " << name << '\n';
			return false;
		}
		if (std::string_view(name) == "animation_with_skeleton.fbx" &&
			(!mesh.isSkinned || mesh.boneCount == 0 || mesh.vertexInfluences.size() != mesh.vertices.size())) return false;
		std::cout << "FBX imported: " << name << " vertices=" << mesh.vertices.size()
			<< " indices=" << mesh.indices.size() << " submeshes=" << mesh.subMeshes.size() << '\n';
	}
	// 以前のDefaultAsset登録をGUIDを変えず更新する
	const auto model = assets.GetPath() / "previous.fbx";
	std::filesystem::copy_file(fixtures / "box.fbx", model);
	AssetMeta previous;
	previous.guid = AssetGUID::New();
	previous.type = AssetType::DefaultAsset;
	previous.importer = "DefaultImporter";
	previous.assetPath = RuntimePaths::ToAssetPath(model);
	if (!AssetMetaStorage::WriteMetaFile(AssetMetaStorage::MetaPathOf(model), previous)) return false;
	AssetDatabase database;
	database.Init();
	if (!database.RebuildMeta({assets.GetPath()})) return false;
	const auto* meta = database.FindByPath(previous.assetPath);
	return meta && meta->guid == previous.guid && meta->type == AssetType::Mesh && meta->importer == "MeshImporter";
}
