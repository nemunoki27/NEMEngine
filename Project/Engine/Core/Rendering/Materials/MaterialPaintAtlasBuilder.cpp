#include "MaterialPaintAtlas.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Watch/PublishedAssetChanges.h>
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Rendering/MaterialPaintComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Externals/xatlas/xatlas.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <future>
#include <map>
#include <optional>

namespace {

	using namespace Engine;

	struct PaintGeometry {

		std::vector<Vector3> positions;
		std::vector<uint32_t> indices;
		std::vector<uint32_t> subMeshes;
		std::vector<uint32_t> meshletTriangles;
	};

	// MeshShader側の並びを元の三角形番号へ戻す
	bool BuildMeshletMap(const ImportedMeshAsset& mesh, PaintGeometry& geometry) {

		using Face = std::array<uint32_t, 3>;
		const auto faceKey = [](Face face) {
			const auto sorted = std::min_element(face.begin(), face.end());
			std::rotate(face.begin(), sorted, face.end());
			return face;
		};
		std::map<Face, std::vector<uint32_t>> faces;
		const auto& indices = mesh.indices;
		for (uint32_t i = 0; i < indices.size(); i += 3) {
			faces[faceKey({ indices[i], indices[i + 1], indices[i + 2] })].push_back(i / 3);
		}
		geometry.meshletTriangles.resize(mesh.meshletPrimitiveIndices.size());
		for (const auto& meshlet : mesh.meshlets) {
			for (uint32_t t = 0; t < meshlet.primitiveCount; ++t) {
				const uint32_t index = meshlet.primitiveOffset + t;
				const auto packed = mesh.meshletPrimitiveIndices[index];
				Face face{};
				for (uint32_t k = 0; k < 3; ++k) {
					face[k] = mesh.meshletVertexIndices[meshlet.vertexOffset + ((packed >> (k * 10)) & 1023u)];
				}
				const auto found = faces.find(faceKey(face));
				if (found == faces.end() || found->second.empty()) return false;
				geometry.meshletTriangles[index] = found->second.back();
				found->second.pop_back();
			}
		}
		return true;
	}

	bool ReadGeometry(const std::filesystem::path& path, AssetID meshID, const PrimitiveRendererComponent* primitive,
		PaintGeometry& geometry, std::string& error) {

		if (primitive) {
			if (primitive->renderSpace != PrimitiveRenderSpace::World3D) {
				error = "3Dの図形を選択してください";
				return false;
			}
			PrimitiveMeshData mesh{};
			PrimitiveMeshGenerator::Generate(*primitive, mesh);
			geometry.positions.reserve(mesh.vertices.size());
			for (const auto& vertex : mesh.vertices) geometry.positions.push_back(vertex.position);
			geometry.indices = std::move(mesh.indices);
			geometry.subMeshes.resize(geometry.indices.size() / 3);
		} else if (meshID) {
			if (path.empty()) return false;
			ImportedMeshAsset mesh;
			try {
				mesh = MeshFileImporter::ImportFile(meshID, path);
			} catch (const std::exception& detail) {
				error = std::string("ペイント対象のメッシュを読み込めません: ") + detail.what();
				return false;
			}
			if (mesh.isSkinned) {
				error = "スキニングメッシュへのペイントには対応していません";
				return false;
			}
			geometry.positions.reserve(mesh.vertices.size());
			for (const auto& vertex : mesh.vertices) {
				const auto& p = vertex.position;
				geometry.positions.emplace_back(p.x, p.y, p.z);
			}
			if (mesh.indices.size() % 3 || mesh.indices.size() > UINT32_MAX || !BuildMeshletMap(mesh, geometry)) return false;
			geometry.indices = std::move(mesh.indices);
			geometry.subMeshes.resize(geometry.indices.size() / 3);
			for (uint32_t i = 0; i < mesh.subMeshes.size(); ++i) {
				const auto& sub = mesh.subMeshes[i];
				std::fill_n(geometry.subMeshes.begin() + sub.indexOffset / 3, sub.indexCount / 3, i);
			}
		}
		return !geometry.indices.empty() && geometry.indices.size() <= UINT32_MAX && geometry.positions.size() <= UINT32_MAX;
	}

	uint64_t GeometryHash(const PaintGeometry& geometry) {

		uint64_t hash = 14695981039346656037ull;
		const auto append = [&](auto& values) {
			for (const auto value : std::as_bytes(std::span(values))) {
				hash = (hash ^ std::to_integer<uint8_t>(value)) * 1099511628211ull;
			}
			hash = (hash ^ values.size()) * 1099511628211ull;
		};
		append(geometry.positions);
		append(geometry.indices);
		append(geometry.subMeshes);
		append(geometry.meshletTriangles);
		return hash;
	}

	struct GeometryCache {
		std::weak_ptr<const uint8_t> lifetime;
		std::map<std::pair<AssetID, uint64_t>, std::string> shapes;
	};

	// アセット変更時か図形パラメータ変更時だけ面の対応を作り直す
	std::string& CachedGeometryKey(const AssetDatabase& database, AssetID mesh, const PrimitiveRendererComponent* primitive) {

		thread_local GeometryCache cache;
		const auto lifetime = database.GetCacheLifetime();
		if (cache.lifetime.owner_before(lifetime) || lifetime.owner_before(cache.lifetime)) {
			cache.shapes.clear();
			cache.lifetime = lifetime;
		}
		if (primitive) return cache.shapes[{ {}, PrimitiveMeshGenerator::ComputeHash(*primitive) }];
		return cache.shapes[{ mesh, database.GetContentRevision(mesh) }];
	}

	bool Unwrap(const PaintGeometry& geometry, MaterialPaintAtlas& result) {

		std::unique_ptr<xatlas::Atlas, decltype(&xatlas::Destroy)> atlas(xatlas::Create(), xatlas::Destroy);
		if (!atlas) return false;
		xatlas::MeshDecl mesh{};
		mesh.vertexCount = static_cast<uint32_t>(geometry.positions.size());
		mesh.vertexPositionData = geometry.positions.data();
		mesh.vertexPositionStride = sizeof(Vector3);
		mesh.indexCount = static_cast<uint32_t>(geometry.indices.size());
		mesh.indexData = geometry.indices.data();
		mesh.indexFormat = xatlas::IndexFormat::UInt32;
		mesh.faceMaterialData = geometry.subMeshes.data();
		if (xatlas::AddMesh(atlas.get(), mesh, 1) != xatlas::AddMeshError::Success) return false;
		xatlas::PackOptions packing{};
		packing.resolution = 1024;
		packing.padding = 8;
		xatlas::Generate(atlas.get(), {}, packing);
		if (atlas->meshCount != 1 || atlas->atlasCount != 1 || !atlas->width || !atlas->height) return false;
		const auto& output = atlas->meshes[0];
		if (output.indexCount != mesh.indexCount) return false;
		result.triangles.resize(mesh.indexCount / 3);
		for (uint32_t i = 0; i < mesh.indexCount; ++i) {
			auto& triangle = result.triangles[i / 3];
			const auto& vertex = output.vertexArray[output.indexArray[i]];
			if (vertex.xref != geometry.indices[i]) return false;
			triangle.positions[i % 3] = geometry.positions[vertex.xref];
			triangle.uv[i % 3] = Vector2(vertex.uv[0] / atlas->width, vertex.uv[1] / atlas->height);
			triangle.subMesh = geometry.subMeshes[i / 3];
		}
		result.meshletTriangles = geometry.meshletTriangles;
		return true;
	}

	bool WriteAtlas(const std::filesystem::path& path, const MaterialPaintAtlas& atlas) {

		const auto count = static_cast<uint32_t>(atlas.triangles.size());
		const auto mapCount = static_cast<uint32_t>(atlas.meshletTriangles.size());
		std::string bytes("NEMPUV01", 8);
		bytes.append(reinterpret_cast<const char*>(&atlas.geometryHash), sizeof(uint64_t));
		bytes.append(reinterpret_cast<const char*>(&count), sizeof(count));
		bytes.append(reinterpret_cast<const char*>(&mapCount), sizeof(mapCount));
		bytes.append(reinterpret_cast<const char*>(atlas.triangles.data()), size_t(count) * sizeof(MaterialPaintTriangle));
		if (mapCount) bytes.append(reinterpret_cast<const char*>(atlas.meshletTriangles.data()), size_t(mapCount) * sizeof(uint32_t));
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) return false;
		StorageFileUtility::FileIdentity identity{};
		return StorageFileUtility::WriteBytesWithoutReplacement(path, bytes, identity) || std::filesystem::is_regular_file(path, ec);
	}

	struct AtlasResult {
		uint64_t key = 0;
		std::string relative, error;
	};

	struct AtlasRequests {
		std::map<std::tuple<std::filesystem::path, AssetID, uint64_t, AssetID>, std::future<AtlasResult>> jobs;
		AssetWorkerPool<std::packaged_task<AtlasResult()>> worker;
		AtlasRequests() {
			worker.Start(1, [](std::packaged_task<AtlasResult()>&& job, uint32_t) { job(); });
		}
	};
}

std::shared_ptr<const Engine::MaterialPaintAtlas> Engine::ResolveMaterialPaintAtlas(AssetDatabase& database,
	AssetID mesh, const PrimitiveRendererComponent* primitive, AssetID& atlasID, std::string& error, bool* preparing) {

	if (preparing) *preparing = false;
	error = "ペイント対象の形状を読み込めません";
	auto& cached = CachedGeometryKey(database, mesh, primitive);
	if (!atlasID && !cached.empty()) {
		uint64_t hash;
		if (std::from_chars(cached.data(), cached.data() + cached.size(), hash).ec == std::errc{}) {
			const auto* meta = database.FindByPath(std::format("GameAssets/PaintData/Atlases/{:016x}.paintatlas", hash));
			if (meta && meta->type == AssetType::PaintAtlas) atlasID = meta->guid;
		}
	}
	if (atlasID && !cached.empty()) {
		auto atlas = LoadMaterialPaintAtlas(database, atlasID);
		if (!atlas || cached != std::to_string(atlas->geometryHash)) {
			error = "保存時から形状が変わったか、ペイントUVを読み込めません";
			return {};
		}
		error.clear();
		return atlas;
	}
	static AtlasRequests requests;
	const auto folder = database.ResolveAssetPath("GameAssets/PaintData/Atlases");
	const auto key = std::make_tuple(folder, mesh, primitive ? PrimitiveMeshGenerator::ComputeHash(*primitive) :
		database.GetContentRevision(mesh), atlasID);
	auto [it, added] = requests.jobs.try_emplace(key);
	if (added) {
		const auto meshPath = mesh ? database.ResolveFullPath(mesh) : std::filesystem::path{};
		const auto shape = primitive ? std::optional(*primitive) : std::nullopt;
		const bool writable = RuntimePaths::CanWritePath(folder / "atlas.paintatlas");
		std::packaged_task<AtlasResult()> job([mesh, meshPath, shape, folder, writable, atlasID] {
			AtlasResult result;
			PaintGeometry geometry;
			if (!ReadGeometry(meshPath, mesh, shape ? &*shape : nullptr, geometry, result.error)) {
				if (result.error.empty()) result.error = "ペイント対象の形状を読み込めません";
				return result;
			}
			result.key = GeometryHash(geometry);
			const auto filename = std::format("{:016x}.paintatlas", result.key);
			result.relative = "GameAssets/PaintData/Atlases/" + filename;
			const auto path = folder / filename;
			std::error_code ec;
			if (!atlasID && !std::filesystem::is_regular_file(path, ec)) {
				MaterialPaintAtlas atlas;
				atlas.geometryHash = result.key;
				if (!Unwrap(geometry, atlas)) result.error = "ペイントUVを展開できません";
				else if (!writable || !WriteAtlas(path, atlas)) result.error = "ペイントUVを保存できません";
			}
			return result;
		});
		it->second = job.get_future();
		requests.worker.Enqueue(std::move(job));
	}
	if (preparing && it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
		*preparing = true;
		error = "ペイントUVを準備しています";
		return {};
	}
	AtlasResult result;
	try { result = it->second.get(); }
	catch (const std::exception& e) { result.error = e.what(); }
	requests.jobs.erase(it);
	if (!result.error.empty()) { error = std::move(result.error); return {}; }
	cached = std::to_string(result.key);
	const auto id = atlasID ? atlasID : PublishedAssetChanges::Import(database, result.relative, AssetType::PaintAtlas);
	auto atlas = LoadMaterialPaintAtlas(database, id);
	if (!atlas || atlas->geometryHash != result.key) {
		error = "ペイントUVと形状が一致しません";
		return {};
	}
	atlasID = id;
	error.clear();
	return atlas;
}

bool Engine::MaterialPaintGeometryMatches(const AssetDatabase& database, const ECSWorld& world,
	Entity entity, const MaterialPaintComponent& paint) {

	if (!paint.atlas || paint.geometryKey.empty() || world.HasComponent<SkinnedAnimationComponent>(entity)) return false;
	const auto* primitive = world.TryGetComponent<PrimitiveRendererComponent>(entity);
	const auto* renderer = world.TryGetComponent<MeshRendererComponent>(entity);
	if (primitive && renderer) return false;
	const auto mesh = renderer ? renderer->mesh : AssetID{};
	if ((!primitive && !mesh) || (primitive && primitive->renderSpace != PrimitiveRenderSpace::World3D)) return false;
	auto& cached = CachedGeometryKey(database, mesh, primitive);
	if (cached.empty()) {
		PaintGeometry geometry;
		std::string error;
		cached = ReadGeometry(mesh ? database.ResolveFullPath(mesh) : std::filesystem::path{}, mesh, primitive, geometry, error) ?
			std::to_string(GeometryHash(geometry)) : "invalid";
	}
	return cached == paint.geometryKey;
}
