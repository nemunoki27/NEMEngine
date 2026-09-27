#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Meshes/MeshImportService.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <fstream>
#include <iostream>

namespace {

	bool TestModelMaterialFactors() {

		using namespace Engine;
		aiMaterial material;
		const aiColor4D pbr(0.2f, 0.4f, 0.6f, 0.5f);
		const aiColor3D diffuse(0.8f, 0.7f, 0.6f);
		const float opacity = 0.25f;
		if (material.AddProperty(&pbr, 1, AI_MATKEY_BASE_COLOR) != AI_SUCCESS ||
			material.AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE) != AI_SUCCESS ||
			material.AddProperty(&opacity, 1, AI_MATKEY_OPACITY) != AI_SUCCESS) return false;
		// 同じMaterialに汎用値があってもPBR色とAlphaを優先する
		const auto factors = MeshImportUtility::ReadMaterialFactors(&material);
		if (!factors.hasBaseColor || factors.baseColor != Color4(0.2f, 0.4f, 0.6f, 0.5f) ||
			factors.hasMetallic || factors.hasRoughness || factors.hasEmissive) return false;
		const auto absent = MeshImportUtility::ReadMaterialFactors(nullptr);
		if (absent.hasBaseColor || absent.baseColor != Color4::White()) return false;

		// 実際のOBJのd値を描画用と編集用の双方へ渡す
		NEMTests::TestDirectory directory("ModelMaterialFactors", RuntimePaths::GetGameAssetsRoot());
		const auto path = directory.GetPath() / "model.obj";
		{
			std::ofstream model(path, std::ios::binary);
			model << "mtllib model.mtl\no Surface\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n";
			model.close();
			if (model.fail()) return false;
			std::ofstream mtl(directory.GetPath() / "model.mtl", std::ios::binary);
			mtl << "newmtl Surface\nKd 0.25 0.5 0.75\nd 0.25\nKe 0.5 0.25 0.125\nPm 0.5\nPr 0.75\n";
			mtl.close();
			if (mtl.fail()) return false;
		}
		AssetDatabase database;
		if (!database.Init()) return false;
		const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
		std::vector<MeshSubMeshLayoutItem> layout;
		MeshImportService importer;
		importer.Init(1);
		if (!mesh || !MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) || layout.size() != 1 ||
			!importer.RequestLoadAsync(database, mesh)) return false;
		importer.WaitAll();
		ImportedMeshAsset imported;
		if (!importer.TakeImported(mesh, imported) || imported.subMeshes.size() != 1) return false;
		const auto& item = layout.front();
		return item.hasBaseColorFactor && item.baseColorFactor == Color4(0.25f, 0.5f, 0.75f, 0.25f) &&
			imported.subMeshes.front().baseColor == item.baseColorFactor &&
			item.sourceSurfaceMode == MaterialSurfaceMode::Transparent &&
			imported.subMeshes.front().surfaceMode == item.sourceSurfaceMode &&
			item.hasEmissiveFactor && item.emissiveFactor == Color4(0.5f, 0.25f, 0.125f, 1.0f) &&
			item.hasMetallicFactor && item.metallicFactor == 0.5f && item.hasRoughnessFactor && item.roughnessFactor == 0.75f;
	}

	bool TestMaterialTextureSelection() {

		using namespace Engine;
		NEMTests::TestDirectory directory("ModelTextures", RuntimePaths::GetGameAssetsRoot());
		const auto& root = directory.GetPath();
		// 画像のデコードを行わず参照解決だけを検証する
		for (const char* name : { "body_diffuse.png", "body_normal.png", "height.png", "metallic.png",
			"roughness.png", "orm.png", "displacement.png", "specular.png", "opacity.png", "emissive.png", "occlusion.png" }) {

			std::ofstream file(root / name, std::ios::binary);
			if (!file) return false;
		}
		// 同順位の候補を逆順に作っても選択を固定する
		for (const char* folder : { "B", "A" }) {
			std::filesystem::create_directory(root / folder);
			std::ofstream file(root / folder / "same.png", std::ios::binary);
			if (!file) return false;
		}
		TextureAssetResolver resolver;
		resolver.Build(root / "model.obj");
		const auto assetPath = [&](const char* name) { return RuntimePaths::ToAssetPath(root / name); };
		if (resolver.ResolveAssetPath("missing/same.png") != assetPath("A/same.png")) return false;
		aiMaterial material;
		const auto set = [&](aiTextureType type, const char* name) {
			const aiString reference(name);
			return material.AddProperty(&reference, AI_MATKEY_TEXTURE(type, 0)) == AI_SUCCESS;
		};
		if (!set(aiTextureType_DIFFUSE, "body_diffuse.png") || !set(aiTextureType_METALNESS, "metallic.png") ||
			!set(aiTextureType_DIFFUSE_ROUGHNESS, "roughness.png") || !set(aiTextureType_SPECULAR, "specular.png") ||
			!set(aiTextureType_OPACITY, "opacity.png") ||
			!set(aiTextureType_EMISSIVE, "emissive.png") || !set(aiTextureType_AMBIENT_OCCLUSION, "occlusion.png")) return false;
		auto textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.baseColorTexturePath != assetPath("body_diffuse.png") ||
			textures.normalTexturePath != assetPath("body_normal.png") ||
			textures.metallicTexturePath != assetPath("metallic.png") ||
			textures.roughnessTexturePath != assetPath("roughness.png") || !textures.metallicRoughnessTexturePath.empty() ||
			textures.specularTexturePath != assetPath("specular.png") ||
			textures.opacityTexturePath != assetPath("opacity.png") ||
			textures.emissiveTexturePath != assetPath("emissive.png") ||
			textures.occlusionTexturePath != assetPath("occlusion.png")) return false;
		// HEIGHT単独はNormalへ、明示Normalとの併存時は変位へ渡す
		if (!set(aiTextureType_HEIGHT, "height.png")) return false;
		textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.normalTexturePath != assetPath("height.png") || !textures.displacementTexturePath.empty()) return false;
		if (!set(aiTextureType_NORMALS, "body_normal.png")) return false;
		textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.normalTexturePath != assetPath("body_normal.png") ||
			textures.displacementTexturePath != assetPath("height.png")) return false;
		if (!set(aiTextureType_DISPLACEMENT, "displacement.png") || !set(aiTextureType_METALNESS, "orm.png") ||
			!set(aiTextureType_DIFFUSE_ROUGHNESS, "orm.png")) return false;
		textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.displacementTexturePath != assetPath("displacement.png") ||
			textures.metallicRoughnessTexturePath != assetPath("orm.png") ||
			!textures.metallicTexturePath.empty() || !textures.roughnessTexturePath.empty() ||
			!resolver.ResolveAssetPath("*0").empty()) return false;
		// 配布用の一覧にも推定Normalを含め、同じ画像は一度だけ渡す
		if (!set(aiTextureType_SPECULAR, "body_diffuse.png")) return false;
		const auto paths = AssimpMaterialTextureExtractor::CollectResolvedPaths(&material, resolver);
		return paths.size() == 7 && std::count(paths.begin(), paths.end(), assetPath("body_diffuse.png")) == 1 &&
			std::find(paths.begin(), paths.end(), assetPath("body_normal.png")) != paths.end() &&
			AssimpMaterialTextureExtractor::CollectResolvedPaths(nullptr, resolver).empty();
	}

	bool TestTriangleMeshSelection() {

		using namespace Engine;
		NEMTests::TestDirectory directory("MeshTriangleSelection", RuntimePaths::GetGameAssetsRoot());
		const auto path = directory.GetPath() / "mixed.obj";
		{
			// 描画面より先に点・線を配置する
			std::ofstream file(path, std::ios::binary);
			file << "v 10 10 10\nv 11 10 10\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
				"o Point\np 1\no Line\nl 1 2\no Surface\nf 3 4 5\n";
			file.close();
			if (file.fail()) return false;
		}
		AssetDatabase database;
		if (!database.Init()) return false;
		const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
		std::vector<MeshSubMeshLayoutItem> layout;
		MeshImportService importer;
		importer.Init(1);
		if (!mesh || !MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) ||
			layout.size() != 1 || layout[0].name != "Surface" || !importer.RequestLoadAsync(database, mesh)) return false;
		importer.WaitAll();
		ImportedMeshAsset imported;
		if (!importer.TakeImported(mesh, imported) || imported.subMeshes.size() != 1 ||
			imported.subMeshes[0].name != layout[0].name || imported.vertices.size() != 3 ||
			imported.lods[0].indexCount != 3 || imported.vertexSubMeshIndices != std::vector<uint32_t>(3, 0)) return false;
		for (uint32_t index : imported.indices) {
			if (index >= imported.vertices.size()) return false;
		}
		{
			// 三角形がなくなった更新は読込失敗として扱う
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << "v 0 0 0\nv 1 0 0\no Line\nl 1 2\n";
			file.close();
			if (file.fail()) return false;
		}
		database.NotifyContentChanged(mesh);
		if (MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) || !layout.empty() ||
			!importer.RequestLoadAsync(database, mesh)) return false;
		importer.WaitAll();
		return importer.ConsumeFailed(mesh) && !importer.TakeImported(mesh, imported);
	}

	bool TestTriangleSkeletonSelection() {

		// 描画対象外のMeshが持つ骨をSkeletonへ混ぜない
		aiScene scene;
		scene.mRootNode = new aiNode("Root");
		scene.mNumMeshes = 2;
		scene.mMeshes = new aiMesh*[2]{};
		for (uint32_t index = 0; index < 2; ++index) {
			auto* mesh = new aiMesh;
			scene.mMeshes[index] = mesh;
			mesh->mNumVertices = 3;
			mesh->mVertices = new aiVector3D[3]{};
			mesh->mNumFaces = 1;
			mesh->mFaces = new aiFace[1];
			mesh->mFaces[0].mNumIndices = index + 2;
			mesh->mFaces[0].mIndices = new unsigned int[index + 2]{};
			mesh->mNumBones = 1;
			mesh->mBones = new aiBone*[1]{ new aiBone };
			mesh->mBones[0]->mNode = scene.mRootNode;
		}
		const auto skeleton = Engine::BuildSkinSkeleton(&scene, "TriangleSelection");
		if (skeleton.joints.size() != 1) return false;
		// 有効な面の骨を外せば点・線の骨だけでは成立しない
		delete scene.mMeshes[1]->mBones[0];
		delete[] scene.mMeshes[1]->mBones;
		scene.mMeshes[1]->mBones = nullptr;
		scene.mMeshes[1]->mNumBones = 0;
		return Engine::BuildSkinSkeleton(&scene, "TriangleSelection").joints.empty();
	}
}

bool NEMTests::TestMeshAuthoringCache() {

	using namespace Engine;
	TestDirectory directory("MeshAuthoring", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "layout.obj";
	const auto write = [&](const char* name) {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << "o " << name << "\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
		file.close();
		return !file.fail();
	};
	AssetDatabase database;
	if (!database.Init() || !write("First")) return false;
	const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
	if (!mesh) return false;
	const auto check = [&](AssetDatabase& source, const char* expected) {
		std::vector<MeshSubMeshLayoutItem> layout;
		const bool valid = MeshSubMeshAuthoring::TryBuildLayout(&source, mesh, layout) &&
			layout.size() == 1 && layout[0].name == expected;
		if (!valid) std::cerr << "Mesh layout cache: " << expected << '\n';
		return valid;
	};
	if (!check(database, "First")) return false;
	AssetDatabase copied = database;
	// Assetの集合が同じでも内容通知で再解析する
	const uint64_t structure = database.GetStructureRevision();
	if (!write("Second")) return false;
	database.NotifyContentChanged(mesh);
	if (database.GetStructureRevision() != structure || !check(database, "Second")) return false;
	// 同じ更新番号の別索引へ結果を流用しない
	if (!write("Third")) return false;
	copied.NotifyContentChanged(mesh);
	if (copied.GetContentRevision(mesh) != database.GetContentRevision(mesh) || !check(copied, "Third")) return false;
	const auto previousLifetime = database.GetCacheLifetime();
	database = copied;
	if (!previousLifetime.expired() || !write("Fourth") || !check(database, "Fourth")) return false;
	// コピー代入後も元の索引の結果は維持する
	if (!check(copied, "Third")) return false;
	// 明示的な初期化は配列の並びに関係なく編集値を解除する
	std::vector<MeshSubMeshLayoutItem> layout(1);
	layout[0].name = "Mesh";
	layout[0].hasRoughnessFactor = true;
	layout[0].roughnessFactor = 0.65f;
	SubMeshMaterial edited;
	edited.name = "Mesh";
	edited.stableID = Engine::UUID::New();
	edited.material = AssetID::New();
	edited.materialInstance.Set("custom", MaterialParameterValue{ .value = 17u });
	std::vector<SubMeshMaterial> matched{ edited }, changed{ edited };
	changed[0].name = "Previous";
	if (!MeshSubMeshAuthoring::SyncComponentToLayout(layout, matched, false) ||
		!MeshSubMeshAuthoring::SyncComponentToLayout(layout, changed, false) ||
		matched[0].material || changed[0].material || matched[0].stableID == edited.stableID ||
		changed[0].stableID == edited.stableID || matched[0].materialInstance.FindByName("custom") ||
		matched[0].materialInstance.GetContentHash() != changed[0].materialInstance.GetContentHash()) return false;
	const auto* roughness = matched[0].materialInstance.FindByName(MaterialParameterNames::Roughness);
	return roughness && std::get<float>(roughness->value) == 0.65f &&
		!MeshSubMeshAuthoring::SyncComponentToLayout(layout, matched, true) &&
		TestTriangleMeshSelection() && TestTriangleSkeletonSelection() && TestMaterialTextureSelection() && TestModelMaterialFactors();
}
