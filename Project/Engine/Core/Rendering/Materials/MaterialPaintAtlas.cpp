#include "MaterialPaintAtlas.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Package/ProductContentPack.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <type_traits>
#include <unordered_map>

namespace {

	using namespace Engine;

	// 64bitの形状ハッシュに続いて面の配列を格納する
	constexpr size_t kHeaderSize = 24;
	static_assert(sizeof(MaterialPaintTriangle) == 64 && std::is_trivially_copyable_v<MaterialPaintTriangle>);

	std::shared_ptr<MaterialPaintAtlas> ReadAtlas(const std::filesystem::path& path) {

		std::vector<uint8_t> packed;
		std::ifstream file;
		size_t size = 0;
		if (ProductContentPack::IsMounted()) {
			if (!ProductContentPack::Read(path, packed)) return {};
			size = packed.size();
		} else {
			file.open(path, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() < 0) return {};
			size = static_cast<size_t>(file.tellg());
			file.seekg(0);
		}
		if (size <= kHeaderSize) return {};
		auto atlas = std::make_shared<MaterialPaintAtlas>();
		std::array<char, kHeaderSize> header{};
		if (file.is_open()) file.read(header.data(), header.size());
		else std::memcpy(header.data(), packed.data(), header.size());
		if (std::memcmp(header.data(), "NEMPUV01", 8) != 0) return {};
		std::memcpy(&atlas->geometryHash, header.data() + 8, sizeof(uint64_t));
		uint32_t count = 0, meshletCount = 0;
		std::memcpy(&count, header.data() + 16, 4);
		std::memcpy(&meshletCount, header.data() + 20, 4);
		const size_t triangleBytes = size_t(count) * sizeof(MaterialPaintTriangle);
		const size_t mapBytes = size_t(meshletCount) * sizeof(uint32_t);
		if (count == 0 || count > UINT32_MAX / 3 || meshletCount > count ||
			size - kHeaderSize != triangleBytes + mapBytes) return {};
		atlas->triangles.resize(count);
		atlas->meshletTriangles.resize(meshletCount);
		if (file.is_open()) {
			if (!file.read(reinterpret_cast<char*>(atlas->triangles.data()), triangleBytes)) return {};
			if (mapBytes && !file.read(reinterpret_cast<char*>(atlas->meshletTriangles.data()), mapBytes)) return {};
		} else {
			std::memcpy(atlas->triangles.data(), packed.data() + kHeaderSize, triangleBytes);
			if (mapBytes) std::memcpy(atlas->meshletTriangles.data(), packed.data() + kHeaderSize + triangleBytes, mapBytes);
		}
		for (const auto index : atlas->meshletTriangles) if (index >= count) return {};
		for (uint32_t i = 0; i < count; ++i) {
			const auto& triangle = atlas->triangles[i];
			if (i == 0 || atlas->batches.back().count == 32 || triangle.subMesh != atlas->triangles[i - 1].subMesh)
				atlas->batches.push_back({triangle.positions.front(), triangle.positions.front(), i, 0});
			auto& batch = atlas->batches.back();
			++batch.count;
			for (size_t corner = 0; corner < 3; ++corner) {
				const auto& p = triangle.positions[corner];
				const auto& uv = triangle.uv[corner];
				if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
					!(0.0f <= uv.x && uv.x <= 1.0f && 0.0f <= uv.y && uv.y <= 1.0f)) return {};
				batch.min = {std::min(batch.min.x,p.x), std::min(batch.min.y,p.y), std::min(batch.min.z,p.z)};
				batch.max = {std::max(batch.max.x,p.x), std::max(batch.max.y,p.y), std::max(batch.max.z,p.z)};
			}
		}
		return atlas;
	}
}

std::shared_ptr<const Engine::MaterialPaintAtlas> Engine::LoadMaterialPaintAtlas(const AssetDatabase& database, AssetID atlasID) {

	if (!atlasID) return {};
	const auto path = database.ResolveFullPath(atlasID);
	if (path.empty()) return {};
	static std::mutex mutex;
	static std::unordered_map<std::filesystem::path, std::weak_ptr<const MaterialPaintAtlas>> loaded;
	std::lock_guard lock(mutex);
	auto& entry = loaded[path];
	if (auto atlas = entry.lock()) return atlas;
	auto atlas = ReadAtlas(path);
	entry = atlas;
	return atlas;
}

void Engine::DilateMaterialPaintMask(const MaterialPaintAtlas& atlas, uint32_t resolution, std::span<uint8_t> rgbaPixels) {

	if (resolution < 1 || resolution > 2048 || rgbaPixels.size() != size_t(resolution) * resolution * 4) return;
	const int side = static_cast<int>(resolution);
	std::vector<uint8_t> covered(size_t(side) * side);
	const auto edge = [](const Vector2& a, const Vector2& b, const Vector2& p) {
		return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
	};
	for (const auto& triangle : atlas.triangles) {
		std::array<Vector2, 3> points;
		for (size_t i = 0; i < 3; ++i) points[i] = triangle.uv[i] * static_cast<float>(side);
		const auto& [a, b, c] = points;
		const float area = edge(a, b, c);
		if (std::abs(area) < 1e-8f) continue;
		const int x0 = std::max(0, static_cast<int>(std::min({a.x, b.x, c.x})));
		const int y0 = std::max(0, static_cast<int>(std::min({a.y, b.y, c.y})));
		const int x1 = std::min(side - 1, static_cast<int>(std::max({a.x, b.x, c.x})));
		const int y1 = std::min(side - 1, static_cast<int>(std::max({a.y, b.y, c.y})));
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				const Vector2 p(x + 0.5f, y + 0.5f);
				if (edge(a, b, p) * area >= 0 && edge(b, c, p) * area >= 0 && edge(c, a, p) * area >= 0) {
					covered[y * side + x] = 1;
				}
			}
		}
	}
	std::vector<int> frontier;
	for (int p = 0; p < side * side; ++p) {
		if (!covered[p]) continue;
		const int x = p % side;
		if ((x > 0 && !covered[p - 1]) || (x + 1 < side && !covered[p + 1]) ||
			(p >= side && !covered[p - side]) || (p + side < side * side && !covered[p + side])) frontier.push_back(p);
	}
	// 同じ距離の外周から順に色を広げる
	for (int step = 0; step < std::max(1, side / 128) && !frontier.empty(); ++step) {
		std::vector<int> next;
		for (const int p : frontier) {
			const int x = p % side;
			for (const int neighbor : { x > 0 ? p - 1 : -1, x + 1 < side ? p + 1 : -1, p - side, p + side }) {
				if (neighbor < 0 || side * side <= neighbor || covered[neighbor]) continue;
				std::memcpy(rgbaPixels.data() + neighbor * 4, rgbaPixels.data() + p * 4, 4);
				covered[neighbor] = 1;
				next.push_back(neighbor);
			}
		}
		frontier.swap(next);
	}
}
