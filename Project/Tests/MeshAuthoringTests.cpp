#include "TestContracts.h"
#include "TestFixtures.h"
#include "ModelDocumentReferenceTests.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Meshes/MeshImportService.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshSurfaceGeometry.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/GIProbeSurfaceMask.h>
#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <fstream>
#include <iostream>

// assimp
#include <assimp/scene.h>

namespace {

	bool TestModelFileDependencies() {

		using namespace Engine;
		NEMTests::TestDirectory directory("ModelDependencies", RuntimePaths::GetGameAssetsRoot());
		const auto root = std::filesystem::absolute(directory.GetPath()).lexically_normal();
		std::ofstream(root / "model.obj")
			<< "mtllib missing.mtl\no Surface\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n";
		std::ofstream(root / "model.mtl") << "newmtl Surface\nmap_Kd -s 1 1 1 color.png\n";
		std::ofstream(root / "color.png", std::ios::binary);
		std::vector<std::filesystem::path> dependencies;
		std::string diagnostic;
		// MTLの補完とTextureのオプションを通常読込と揃える
		if (!ModelFileDependencyCollector::Collect(root / "model.obj", dependencies, diagnostic) || dependencies.size() != 3 ||
			std::find(dependencies.begin(), dependencies.end(), root / "model.mtl") == dependencies.end() ||
			std::find(dependencies.begin(), dependencies.end(), root / "color.png") == dependencies.end()) {
			std::cerr << "OBJ dependencies failed: " << diagnostic << " count=" << dependencies.size() << '\n';
			return false;
		}

		std::filesystem::create_directory(root / "buffers");
		const std::array<float, 9> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0};
		{
			std::ofstream buffer(root / "buffers/geometry0.bin", std::ios::binary);
			buffer.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
			buffer.close();
			if (buffer.fail()) {
				return false;
			}
		}
		std::ofstream(root / "mesh.gltf") << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
"buffers":[{"uri":"buffers/geometry0.bin","byteLength":36}],
"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
"extras":{"uri":"unrelated.bin"}})";
		// 同名ではないBufferを含め、任意のuri文字列は収集しない
		if (!ModelFileDependencyCollector::Collect(root / "mesh.gltf", dependencies, diagnostic) || dependencies.size() != 2 ||
			std::find(dependencies.begin(), dependencies.end(), root / "buffers/geometry0.bin") == dependencies.end()) {
			std::cerr << "glTF dependencies failed: " << diagnostic << " count=" << dependencies.size() << '\n';
			return false;
		}

		// Project外の画像も収集し通常描画へ外部パスを漏らさない
		NEMTests::TestDirectory external("ExternalModelDependencies");
		const auto outside = std::filesystem::absolute(external.GetPath()).lexically_normal();
		std::ofstream(outside / "surface.obj")
			<< "mtllib surface.mtl\no Surface\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n";
		std::ofstream(outside / "surface.mtl") << "newmtl Surface\nmap_Kd external_surface_diffuse.png\n";
		std::ofstream(outside / "external_surface_diffuse.png", std::ios::binary);
		std::ofstream(outside / "external_surface_normal.png", std::ios::binary);
		TextureAssetResolver resolver;
		resolver.Build(outside / "surface.obj");
		if (!RuntimePaths::ToAssetPath(outside).empty() || !resolver.ResolveAssetPath("external_surface_diffuse.png").empty() ||
			resolver.ResolveFilePath("external_surface_diffuse.png") != outside / "external_surface_diffuse.png" ||
			resolver.ResolveNormalFilePath({}, "external_surface_diffuse.png") != outside / "external_surface_normal.png" ||
			!ModelFileDependencyCollector::Collect(outside / "surface.obj", dependencies, diagnostic) ||
			dependencies.size() != 4 ||
			std::find(dependencies.begin(), dependencies.end(), outside / "external_surface_diffuse.png") ==
				dependencies.end() ||
			std::find(dependencies.begin(), dependencies.end(), outside / "external_surface_normal.png") ==
				dependencies.end() ||
			!resolver.ResolveFilePath("*0").empty()) {
			std::cerr << "External dependencies failed: " << diagnostic << " count=" << dependencies.size() << '\n';
			return false;
		}
		return !ModelFileDependencyCollector::Collect(root / "missing.obj", dependencies, diagnostic) && !diagnostic.empty();
	}

	bool TestModelMaterialFactors() {

		using namespace Engine;
		aiMaterial material;
		const aiColor4D pbr(0.2f, 0.4f, 0.6f, 0.5f);
		const aiColor3D diffuse(0.8f, 0.7f, 0.6f);
		const float opacity = 0.25f;
		if (material.AddProperty(&pbr, 1, AI_MATKEY_BASE_COLOR) != AI_SUCCESS ||
			material.AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE) != AI_SUCCESS ||
			material.AddProperty(&opacity, 1, AI_MATKEY_OPACITY) != AI_SUCCESS) {
			return false;
		}
		// 同じMaterialに汎用値があってもPBR色とAlphaを優先する
		const auto factors = MeshImportUtility::ReadMaterialFactors(&material);
		if (!factors.hasBaseColor || factors.baseColor != Color4(0.2f, 0.4f, 0.6f, 0.5f) || factors.hasMetallic ||
			factors.hasRoughness || factors.hasEmissive) {
			return false;
		}
		const auto absent = MeshImportUtility::ReadMaterialFactors(nullptr);
		if (absent.hasBaseColor || absent.baseColor != Color4::White()) {
			return false;
		}

		// 実際のOBJのd値を描画用と編集用の双方へ渡す
		NEMTests::TestDirectory directory("ModelMaterialFactors", RuntimePaths::GetGameAssetsRoot());
		const auto path = directory.GetPath() / "model.obj";
		{
			std::ofstream model(path, std::ios::binary);
			model << "mtllib model.mtl\no Surface\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n";
			model.close();
			if (model.fail()) {
				return false;
			}
			std::ofstream mtl(directory.GetPath() / "model.mtl", std::ios::binary);
			mtl << "newmtl Surface\nKd 0.25 0.5 0.75\nd 0.25\nKe 0.5 0.25 0.125\nPm 0.5\nPr 0.75\n";
			mtl.close();
			if (mtl.fail()) {
				return false;
			}
		}
		AssetDatabase database;
		if (!database.Init()) {
			return false;
		}
		const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
		std::vector<MeshSubMeshLayoutItem> layout;
		MeshImportService importer;
		importer.Init(1);
		if (!mesh || !MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) || layout.size() != 1 ||
			!importer.RequestLoadAsync(database, mesh)) {
			return false;
		}
		importer.WaitAll();
		ImportedMeshAsset imported;
		if (!importer.TakeImported(mesh, imported) || imported.subMeshes.size() != 1) {
			return false;
		}
		const auto& item = layout.front();
		return item.hasBaseColorFactor && item.baseColorFactor == Color4(0.25f, 0.5f, 0.75f, 0.25f) &&
			   imported.subMeshes.front().baseColor == item.baseColorFactor &&
			   item.sourceSurfaceMode == MaterialSurfaceMode::Transparent &&
			   imported.subMeshes.front().surfaceMode == item.sourceSurfaceMode && item.hasEmissiveFactor &&
			   item.emissiveFactor == Color4(0.5f, 0.25f, 0.125f, 1.0f) && item.hasMetallicFactor &&
			   item.metallicFactor == 0.5f && item.hasRoughnessFactor && item.roughnessFactor == 0.75f;
	}

	bool TestMaterialTextureSelection() {

		using namespace Engine;
		NEMTests::TestDirectory directory("ModelTextures", RuntimePaths::GetGameAssetsRoot());
		const auto& root = directory.GetPath();
		// 画像のデコードを行わず参照解決だけを検証する
		for (const char* name : {"body_diffuse.png", "body_normal.png", "height.png", "metallic.png", "roughness.png",
				 "orm.png", "displacement.png", "specular.png", "opacity.png", "emissive.png", "occlusion.png"}) {

			std::ofstream file(root / name, std::ios::binary);
			if (!file) {
				return false;
			}
		}
		// 同順位の候補を逆順に作っても選択を固定する
		for (const char* folder : {"B", "A"}) {
			std::filesystem::create_directory(root / folder);
			std::ofstream file(root / folder / "same.png", std::ios::binary);
			if (!file) {
				return false;
			}
		}
		TextureAssetResolver resolver;
		resolver.Build(root / "model.obj");
		const auto assetPath = [&](const char* name) { return RuntimePaths::ToAssetPath(root / name); };
		if (resolver.ResolveAssetPath("missing/same.png") != assetPath("A/same.png")) {
			return false;
		}
		aiMaterial material;
		const auto set = [&](aiTextureType type, const char* name) {
			const aiString reference(name);
			return material.AddProperty(&reference, AI_MATKEY_TEXTURE(type, 0)) == AI_SUCCESS;
		};
		if (!set(aiTextureType_DIFFUSE, "body_diffuse.png") || !set(aiTextureType_METALNESS, "metallic.png") ||
			!set(aiTextureType_DIFFUSE_ROUGHNESS, "roughness.png") || !set(aiTextureType_SPECULAR, "specular.png") ||
			!set(aiTextureType_OPACITY, "opacity.png") || !set(aiTextureType_EMISSIVE, "emissive.png") ||
			!set(aiTextureType_AMBIENT_OCCLUSION, "occlusion.png")) {
			return false;
		}
		auto textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.baseColorTexturePath != assetPath("body_diffuse.png") ||
			textures.normalTexturePath != assetPath("body_normal.png") ||
			textures.metallicTexturePath != assetPath("metallic.png") ||
			textures.roughnessTexturePath != assetPath("roughness.png") || !textures.metallicRoughnessTexturePath.empty() ||
			textures.specularTexturePath != assetPath("specular.png") ||
			textures.opacityTexturePath != assetPath("opacity.png") ||
			textures.emissiveTexturePath != assetPath("emissive.png") ||
			textures.occlusionTexturePath != assetPath("occlusion.png")) {
			return false;
		}
		// HEIGHT単独はNormalへ、明示Normalとの併存時は変位へ渡す
		if (!set(aiTextureType_HEIGHT, "height.png")) {
			return false;
		}
		textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.normalTexturePath != assetPath("height.png") || !textures.displacementTexturePath.empty()) {
			return false;
		}
		if (!set(aiTextureType_NORMALS, "body_normal.png")) {
			return false;
		}
		textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.normalTexturePath != assetPath("body_normal.png") ||
			textures.displacementTexturePath != assetPath("height.png")) {
			return false;
		}
		if (!set(aiTextureType_DISPLACEMENT, "displacement.png") || !set(aiTextureType_METALNESS, "orm.png") ||
			!set(aiTextureType_DIFFUSE_ROUGHNESS, "orm.png")) {
			return false;
		}
		textures = AssimpMaterialTextureExtractor::ExtractResolved(&material, resolver);
		if (textures.displacementTexturePath != assetPath("displacement.png") ||
			textures.metallicRoughnessTexturePath != assetPath("orm.png") || !textures.metallicTexturePath.empty() ||
			!textures.roughnessTexturePath.empty() || !resolver.ResolveAssetPath("*0").empty()) {
			return false;
		}
		// 配布用の一覧にも推定Normalを含め、同じ画像は一度だけ渡す
		if (!set(aiTextureType_SPECULAR, "body_diffuse.png")) {
			return false;
		}
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
			if (file.fail()) {
				return false;
			}
		}
		AssetDatabase database;
		if (!database.Init()) {
			return false;
		}
		const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
		std::vector<MeshSubMeshLayoutItem> layout;
		MeshImportService importer;
		importer.Init(1);
		if (!mesh || !MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) || layout.size() != 1 ||
			layout[0].name != "Surface" || !importer.RequestLoadAsync(database, mesh)) {
			return false;
		}
		importer.WaitAll();
		ImportedMeshAsset imported;
		if (!importer.TakeImported(mesh, imported) || imported.subMeshes.size() != 1 ||
			imported.subMeshes[0].name != layout[0].name || imported.vertices.size() != 3 || imported.indices.size() != 3 ||
			imported.vertexSubMeshIndices != std::vector<uint32_t>(3, 0)) {
			return false;
		}
		for (uint32_t index : imported.indices) {
			if (index >= imported.vertices.size()) {
				return false;
			}
		}
		{
			// 三角形がなくなった更新は読込失敗として扱う
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << "v 0 0 0\nv 1 0 0\no Line\nl 1 2\n";
			file.close();
			if (file.fail()) {
				return false;
			}
		}
		database.NotifyContentChanged(mesh);
		if (MeshSubMeshAuthoring::TryBuildLayout(&database, mesh, layout) || !layout.empty() ||
			!importer.RequestLoadAsync(database, mesh)) {
			return false;
		}
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
			mesh->mBones = new aiBone*[1]{new aiBone};
			mesh->mBones[0]->mNode = scene.mRootNode;
		}
		const auto skeleton = Engine::BuildSkinSkeleton(&scene, "TriangleSelection");
		if (skeleton.joints.size() != 1) {
			return false;
		}
		// 有効な面の骨を外せば点・線の骨だけでは成立しない
		delete scene.mMeshes[1]->mBones[0];
		delete[] scene.mMeshes[1]->mBones;
		scene.mMeshes[1]->mBones = nullptr;
		scene.mMeshes[1]->mNumBones = 0;
		return Engine::BuildSkinSkeleton(&scene, "TriangleSelection").joints.empty();
	}
}

bool NEMTests::TestGIProbeSurfaceMask() {

	using namespace Engine;
	ImportedMeshAsset mesh;
	for (uint32_t triangle = 0; triangle < 20; ++triangle) {
		const float x = 4.0f * triangle;
		for (const Vector4 position : {Vector4{x,0,0,1}, Vector4{x+1,0,0,1}, Vector4{x,1,0,1}}) {
			mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
			mesh.vertices.emplace_back().position = position;
		}
	}
	mesh.subMeshes.resize(2);
	mesh.subMeshes[0].indexCount = 57;
	mesh.subMeshes[1].indexOffset = 57;
	mesh.subMeshes[1].indexCount = 3;
	const auto surface = MeshSurfaceGeometry::Build(mesh);
	if (!surface || !surface->IntersectsSphere(0, {.25f,.25f,.49f}, .5f) ||
		surface->IntersectsSphere(0, {.25f,.25f,.51f}, .5f) ||
		surface->IntersectsSphere(0, {.9f,.9f,0}, .1f) ||
		surface->IntersectsSphere(0, {76.25f,.25f,0}, .1f) ||
		!surface->IntersectsSphere(1, {76.25f,.25f,0}, .1f)) return false;
	mesh.isSkinned = true;
	if (MeshSurfaceGeometry::Build(mesh)) return false;
	mesh.boneCount = 2;
	mesh.vertexInfluences.resize(mesh.vertices.size());
	for (uint32_t i = 0; i < mesh.vertices.size(); ++i) {
		auto& weights = mesh.vertexInfluences[i];
		weights.jointIndices[0] = i < 57 ? 0 : 1;
		weights.weights[0] = 1;
	}
	// 遠く離れた別サブメッシュのボーン、混合ウェイト、未加重頂点を分けて扱う
	mesh.vertexInfluences[0].jointIndices[1] = 1;
	mesh.vertexInfluences[0].weights = {2, 3, 0, 0};
	mesh.vertexInfluences.back() = {};
	auto skin = MeshSurfaceGeometry::Build(mesh);
	if (!skin) return false;
	std::array<WellForGPU, 2> pose{};
	pose[0].skeletonSpaceMatrix.m[3][0] = 1000;
	pose[1].skeletonSpaceMatrix.m[3][0] = 10;
	pose[1].skeletonSpaceMatrix.m[3][1] = 2;
	pose[1].skeletonSpaceMatrix.m[3][2] = -3;
	const auto contains = [](const Vector4& sphere, const Vector3& p) {
		return (Vector3{sphere.x, sphere.y, sphere.z} - p).Length() <= sphere.w;
	};
	const auto firstSkin = skin->GetSkinnedBounds(0, pose);
	const auto lastSkin = skin->GetSkinnedBounds(1, pose);
	if (!contains(firstSkin.sphere, {406, 1.2f, -1.8f}) || !contains(lastSkin.sphere, {86, 2, -3}) ||
		!contains(lastSkin.sphere, {87, 2, -3}) || !contains(lastSkin.sphere, {76, 1, 0}) || lastSkin.sphere.w >= 6 ||
		lastSkin.halfExtent.x < 5.5f || lastSkin.halfExtent.x > 5.51f || lastSkin.halfExtent.z < 1.5f) return false;
	pose[0].skeletonSpaceMatrix.m[3][0] = -2000;
	if (skin->GetSkinnedBounds(1, pose).sphere != lastSkin.sphere || skin->GetSkinnedBounds(1, std::span(pose).first(1)).sphere.w >= 0) return false;
	auto local = Matrix4x4::Identity();
	local.m[0][0] = -2;
	local.m[0][1] = 3;
	local.m[2][2] = -0.5f;
	local.m[3][1] = 9;
	const Vector3 bindCenter{lastSkin.sphere.x, lastSkin.sphere.y, lastSkin.sphere.z};
	const auto transformed = MeshBoundsUtility::TransformHalfExtent(local, bindCenter, lastSkin.halfExtent);
	for (const Vector3 p : {Vector3{86,2,-3}, Vector3{87,2,-3}, Vector3{76,1,0}}) {
		const auto delta = Vector3::Transform(p, local) - Vector3::Transform(bindCenter, local);
		if (std::abs(delta.x) > transformed.x || std::abs(delta.y) > transformed.y || std::abs(delta.z) > transformed.z) return false;
	}
	pose[1].skeletonSpaceMatrix.m[3][0] = std::numeric_limits<float>::infinity();
	if (skin->GetSkinnedBounds(1, pose).sphere.w >= 0) return false;
	mesh.vertexInfluences[0].jointIndices[0] = 7;
	if (MeshSurfaceGeometry::Build(mesh)) return false;

	GIProbeSurfaceMask mask;
	SceneExecutionContext context{};
	std::array<Matrix4x4,1> worlds{Matrix4x4::Identity()};
	RaytracingGeometryBounds bounds{};
	bounds.probeBoundsGuaranteed = true;
	bounds.probeBounds = {0,0,0,100};
	bounds.probeSurface = surface;
	context.raytracing.instanceWorldMatrices = worlds;
	context.raytracing.geometryBounds = {&bounds,1};
	context.raytracing.probeReceiversCovered = true;
	const std::array<GIProbeSurfaceMask::Brick,1> bricks{{{0,0,0,1}}};
	std::vector<uint64_t> live;
	const auto update = [&] { mask.Update(context, bricks, .75f, 2, live); };
	const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	do {
		update();
		if (live.size() != 1) return false;
		if (live.front() != UINT64_MAX) break;
		std::this_thread::yield();
	} while (std::chrono::steady_clock::now() < end);
	if ((live.front() & 1u) == 0 || (live.front() & (uint64_t{1} << 63)) != 0) return false;
	// スキニングは変形後の境界を使い、前フレームの剛体メッシュを参照しない
	bounds.deformed = true;
	bounds.probeBounds = {.375f,.375f,.375f,0};
	bounds.radius = .02f;
	GIProbeSurfaceMask::Bounds dirtyBounds;
	if (!GIProbeSurfaceMask::TryGetWorldBounds(bounds, worlds[0], dirtyBounds) || dirtyBounds.sphere != bounds.probeBounds) return false;
	update();
	if ((live.front() & 1u) == 0 || (live.front() & (uint64_t{1} << 63)) != 0) return false;
	worlds[0].m[3][0] = 1000;
	if (!GIProbeSurfaceMask::TryGetWorldBounds(bounds, worlds[0], dirtyBounds) || dirtyBounds.sphere.x != 1000.375f) return false;
	update();
	if (live.front() != 0) return false;
	// 非一様・負のスケールとせん断を含むボーン境界も、全ての角を含める
	bounds.probeBounds = {0, 0, 0, 5};
	bounds.probeHalfExtent = {2, 1, .25f};
	worlds[0] = Matrix4x4::Identity();
	worlds[0].m[0][0] = -2;
	worlds[0].m[0][1] = 3;
	worlds[0].m[2][2] = .5f;
	if (!GIProbeSurfaceMask::TryGetWorldBounds(bounds, worlds[0], dirtyBounds)) return false;
	for (int corner = 0; corner < 8; ++corner) {
		const Vector3 point{(corner & 1) ? 2.0f : -2.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? .25f : -.25f};
		const Vector3 p = Vector3::Transform(point, worlds[0]);
		if (!(dirtyBounds.GetProbeMask(p, 1, .001f, 0) & 1)) return false;
	}
	if ((dirtyBounds.GetProbeMask({0, 0, .5f}, 1, .25f, 0) & 1u) != 0 ||
		dirtyBounds.GetProbeMask({}, 1, 1, UINT64_MAX) != 0) return false;
	// 未知の境界とTLAS外の受光面はどちらも全プローブ有効へ戻す
	bounds.probeBoundsGuaranteed = false;
	if (GIProbeSurfaceMask::TryGetWorldBounds(bounds, worlds[0], dirtyBounds)) return false;
	update();
	if (!live.empty()) return false;
	bounds.probeBoundsGuaranteed = true;
	context.raytracing.probeReceiversCovered = false;
	update();
	return live.empty();
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
	if (!database.Init() || !write("First")) {
		return false;
	}
	const AssetID mesh = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
	if (!mesh) {
		return false;
	}
	const auto check = [&](AssetDatabase& source, const char* expected) {
		std::vector<MeshSubMeshLayoutItem> layout;
		const bool valid =
			MeshSubMeshAuthoring::TryBuildLayout(&source, mesh, layout) && layout.size() == 1 && layout[0].name == expected;
		if (!valid) {
			std::cerr << "Mesh layout cache: " << expected << '\n';
		}
		return valid;
	};
	if (!check(database, "First")) {
		return false;
	}
	AssetDatabase copied = database;
	// Assetの集合が同じでも内容通知で再解析する
	const uint64_t structure = database.GetStructureRevision();
	if (!write("Second")) {
		return false;
	}
	database.NotifyContentChanged(mesh);
	if (database.GetStructureRevision() != structure || !check(database, "Second")) {
		return false;
	}
	// 同じ更新番号の別索引へ結果を流用しない
	if (!write("Third")) {
		return false;
	}
	copied.NotifyContentChanged(mesh);
	if (copied.GetContentRevision(mesh) != database.GetContentRevision(mesh) || !check(copied, "Third")) {
		return false;
	}
	const auto previousLifetime = database.GetCacheLifetime();
	database = copied;
	if (!previousLifetime.expired() || !write("Fourth") || !check(database, "Fourth")) {
		return false;
	}
	// コピー代入後も元の索引の結果は維持する
	if (!check(copied, "Third")) {
		return false;
	}
	// 明示的な初期化は配列の並びに関係なく編集値を解除する
	std::vector<MeshSubMeshLayoutItem> layout(1);
	layout[0].name = "Mesh";
	layout[0].hasRoughnessFactor = true;
	layout[0].roughnessFactor = 0.65f;
	SubMeshMaterial edited;
	edited.name = "Mesh";
	edited.stableID = Engine::UUID::New();
	edited.material = AssetID::New();
	edited.materialInstance.Set("custom", MaterialParameterValue{.value = 17u});
	std::vector<SubMeshMaterial> matched{edited}, changed{edited};
	changed[0].name = "Previous";
	if (!MeshSubMeshAuthoring::SyncComponentToLayout(layout, matched, false) ||
		!MeshSubMeshAuthoring::SyncComponentToLayout(layout, changed, false) || matched[0].material || changed[0].material ||
		matched[0].stableID == edited.stableID || changed[0].stableID == edited.stableID ||
		matched[0].materialInstance.FindByName("custom") ||
		matched[0].materialInstance.GetContentHash() != changed[0].materialInstance.GetContentHash()) {
		return false;
	}
	// Autoのモデルでも同期や並べ替えで編集済みカットオフを上書きしない
	matched[0].alphaCutoff = 0.75f;
	if (MeshSubMeshAuthoring::SyncComponentToLayout(layout, matched, true) || matched[0].alphaCutoff != 0.75f) {
		return false;
	}
	changed = matched;
	changed[0].name = "Previous";
	if (!MeshSubMeshAuthoring::SyncComponentToLayout(layout, changed, true) || changed[0].alphaCutoff != 0.75f) {
		return false;
	}
	layout[0].sourceSurfaceMode = MaterialSurfaceMode::Masked;
	if (!MeshSubMeshAuthoring::SyncComponentToLayout(layout, matched, true) || matched[0].alphaCutoff != 0.75f) {
		return false;
	}
	const auto* roughness = matched[0].materialInstance.FindByName(MaterialParameterNames::Roughness);
	return roughness && std::get<float>(roughness->value) == 0.65f &&
		   !MeshSubMeshAuthoring::SyncComponentToLayout(layout, matched, true) &&
		   RunTest("GIProbeSurfaceMask", TestGIProbeSurfaceMask) &&
		   RunTest("TriangleMeshSelection", TestTriangleMeshSelection) &&
		   RunTest("TriangleSkeletonSelection", TestTriangleSkeletonSelection) &&
		   RunTest("MaterialTextureSelection", TestMaterialTextureSelection) &&
		   RunTest("ModelMaterialFactors", TestModelMaterialFactors) &&
		   RunTest("ModelFileDependencies", TestModelFileDependencies) &&
		   RunTest("ModelDocumentReferences", CheckModelDocumentReferences);
}
