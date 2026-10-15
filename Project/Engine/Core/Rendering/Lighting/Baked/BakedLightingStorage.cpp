#include "BakedLightingStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <cstring>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace {

	constexpr uint32_t kFormatVersion = 1;
	constexpr size_t kMaximumBytes = 512ull * 1024ull * 1024ull;

	// 配列の型配置を保って保存する
	template<typename T>
	void Append(std::string& bytes, const std::vector<T>& values) {

		static_assert(std::is_trivially_copyable_v<T>);
		const uint32_t count = static_cast<uint32_t>(values.size());
		if (values.size() > UINT32_MAX || bytes.size() + values.size() * sizeof(T) + sizeof(count) > kMaximumBytes) {
			throw std::runtime_error("ベイク結果のサイズが上限を超えています");
		}
		bytes.append(reinterpret_cast<const char*>(&count), sizeof(count));
		if (!values.empty()) bytes.append(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T));
	}
	// 要素数と残りサイズを検証して復元する
	template<typename T>
	void Read(std::span<const char>& bytes, std::vector<T>& output) {

		if (bytes.size() < sizeof(uint32_t)) throw std::runtime_error("ベイク結果の配列Headerが不足しています");
		uint32_t count;
		std::memcpy(&count, bytes.data(), sizeof(count));
		bytes = bytes.subspan(sizeof(count));
		if (size_t(count) > bytes.size() / sizeof(T)) throw std::runtime_error("ベイク結果の配列が不足しています");
		output.resize(count);
		if (count) std::memcpy(output.data(), bytes.data(), size_t(count) * sizeof(T));
		bytes = bytes.subspan(size_t(count) * sizeof(T));
	}
}

Engine::AssetID Engine::BakedLightingStorage::Save(AssetDatabase& database, const std::string& path,
	const BakedLightingData& data, std::string& diagnostic) try {

	Validate(data);
	std::string bytes;
	Append(bytes, std::vector<uint32_t>{0x4e454d42u, kFormatVersion});
	Append(bytes, data.triangles);
	Append(bytes, data.nodes);
	Append(bytes, data.triangleOrder);
	Append(bytes, data.lightmaps);
	Append(bytes, data.probes);
	Append(bytes, data.captures);
	Append(bytes, data.reflectionPixels);
	std::array<AssetDocumentChange, 2> changes;
	// バイナリと文書の復旧を既存の保存処理へまとめる
	if (!AssetDocumentPublication::Prepare(database, path + ".bin", AssetType::DefaultAsset, changes[0], diagnostic) ||
		!AssetDocumentPublication::Prepare(database, path, AssetType::BakedLighting, changes[1], diagnostic)) return {};
	changes[0].bytes = std::move(bytes);
	nlohmann::json receivers = nlohmann::json::array();
	for (const auto& receiver : data.receivers) {
		receivers.push_back({{"localFileID", ToString(receiver.localFileID)}, {"worldMatrix", receiver.worldMatrix.m},
			{"settings", receiver.settings}});
	}
	changes[1].document = {{"schemaVersion", kFormatVersion}, {"sourceSceneGUID", ToAssetReferenceJson(data.sceneAsset)},
		{"bakedLightingData", ToAssetReferenceJson(changes[0].metadata.guid)}, {"atlasSize", data.atlasSize},
		{"atlasCount", data.atlasCount}, {"surfaceTolerance", data.surfaceTolerance}, {"probeSpacing", data.probeSpacing},
		{"probeMinimum", data.probeMinimum.ToJson()}, {"probeDimensions", data.probeDimensions}, {"inputRevisions", data.inputRevisions},
		{"receivers", std::move(receivers)}};
	if (!AssetDocumentPublication::Commit(database, changes, AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::GeneratedRender),
		diagnostic)) return {};
	return changes[1].metadata.guid;
} catch (const std::exception& error) {
	diagnostic = error.what();
	return {};
}

Engine::BakedLightingData Engine::BakedLightingStorage::Load(const std::filesystem::path& path,
	const std::filesystem::path& dataPath) {

	const auto document = JsonFile::Load(path, false);
	if (!document.is_object() || document.value("schemaVersion", 0u) != kFormatVersion) {
		throw std::runtime_error("ベイク結果の保存形式を認識できません");
	}
	const uintmax_t byteCount = std::filesystem::file_size(dataPath);
	if (byteCount > kMaximumBytes) throw std::runtime_error("ベイク結果のサイズが上限を超えています");
	std::string bytes(size_t(byteCount), '\0');
	std::ifstream stream(dataPath, std::ios::binary);
	if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("ベイク結果を読み込めません");
	std::span<const char> remaining(bytes);
	std::vector<uint32_t> header;
	Read(remaining, header);
	if (header != std::vector<uint32_t>{0x4e454d42u, kFormatVersion}) throw std::runtime_error("ベイク結果のHeaderが不正です");
	BakedLightingData data;
	Read(remaining, data.triangles);
	Read(remaining, data.nodes);
	Read(remaining, data.triangleOrder);
	Read(remaining, data.lightmaps);
	Read(remaining, data.probes);
	Read(remaining, data.captures);
	Read(remaining, data.reflectionPixels);
	if (!remaining.empty()) throw std::runtime_error("ベイク結果に未登録のデータがあります");
	data.sceneAsset = ParseAssetID(document, "sourceSceneGUID");
	data.atlasSize = document.at("atlasSize").get<uint32_t>();
	data.atlasCount = document.at("atlasCount").get<uint32_t>();
	data.surfaceTolerance = document.value("surfaceTolerance", 0.05f);
	data.probeSpacing = document.value("probeSpacing", 4.0f);
	data.probeMinimum = Vector3::FromJson(document.at("probeMinimum"));
	data.probeDimensions = document.at("probeDimensions").get<std::array<uint32_t, 3>>();
	data.inputRevisions = document.at("inputRevisions");
	for (const auto& receiver : document.at("receivers")) {
		Matrix4x4 matrix{};
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) matrix.m[row][column] = receiver.at("worldMatrix").at(row).at(column).get<float>();
		}
		data.receivers.push_back({FromString16Hex(receiver.at("localFileID").get<std::string>()), matrix,
			receiver.at("settings").get<std::string>()});
	}
	Validate(data);
	return data;
}

void Engine::BakedLightingStorage::Validate(const BakedLightingData& data) {

	const auto finite3 = [](const Vector3& value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	};
	const auto finite4 = [](const Vector4& value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
	};
	const auto invalid = []() { throw std::runtime_error("ベイク結果の範囲または数値が不正です"); };
	if (data.atlasSize == 0 || data.atlasSize > 4096 || data.atlasCount == 0 || data.atlasCount > 32 ||
		data.lightmaps.size() != uint64_t(data.atlasSize) * data.atlasSize * data.atlasCount ||
		data.triangleOrder.size() != data.triangles.size() || data.triangles.empty() || data.nodes.empty() ||
		!std::isfinite(data.surfaceTolerance) || data.surfaceTolerance <= 0.0f || !finite3(data.probeMinimum) ||
		!std::isfinite(data.probeSpacing) || data.probeSpacing <= 0.0f || data.probes.size() > 4096 || data.captures.size() > 64) invalid();
	const auto& dimensions = data.probeDimensions;
	if (!data.probes.empty() && (std::min({dimensions[0], dimensions[1], dimensions[2]}) < 2 ||
		uint64_t(dimensions[0]) * dimensions[1] * dimensions[2] != data.probes.size())) invalid();
	// 探索順と参照先が全て保存配列の範囲内か確認する
	std::vector<bool> visitedTriangles(data.triangles.size());
	for (uint32_t index : data.triangleOrder) {
		if (index >= data.triangles.size() || visitedTriangles[index]) invalid();
		visitedTriangles[index] = true;
	}
	for (const auto& triangle : data.triangles) {
		if (triangle.atlasIndex >= data.atlasCount || triangle.receiverIndex >= data.receivers.size()) invalid();
		for (const auto& vertex : triangle.vertices) {
			if (!finite3(vertex.position) || !finite3(vertex.normal) || !std::isfinite(vertex.lightmapU) ||
				!std::isfinite(vertex.lightmapV) || vertex.lightmapU < 0.0f || vertex.lightmapU > 1.0f ||
				vertex.lightmapV < 0.0f || vertex.lightmapV > 1.0f) invalid();
		}
	}
	std::vector<uint32_t> stack{0};
	std::vector<bool> visitedNodes(data.nodes.size());
	while (!stack.empty()) {
		const uint32_t index = stack.back();
		stack.pop_back();
		if (index >= data.nodes.size() || visitedNodes[index]) invalid();
		visitedNodes[index] = true;
		const auto& node = data.nodes[index];
		if (!finite3(node.minimum) || !finite3(node.maximum) || node.minimum.x > node.maximum.x ||
			node.minimum.y > node.maximum.y || node.minimum.z > node.maximum.z) invalid();
		if (node.left & 0x80000000u) {
			if (node.right == 0 || uint64_t(node.left & 0x7FFFFFFFu) + node.right > data.triangleOrder.size()) invalid();
		} else {
			stack.push_back(node.left);
			stack.push_back(node.right);
		}
	}
	for (const auto& value : data.lightmaps) if (!finite4(value)) invalid();
	for (const auto& value : data.reflectionPixels) if (!finite4(value)) invalid();
	for (const auto& probe : data.probes) {
		if (!finite3(probe.position) || !std::isfinite(probe.radius) || probe.radius < 0.0f) invalid();
		for (const auto& value : probe.irradiance) if (!finite4(value)) invalid();
		for (const auto& value : probe.visibility) if (!finite4(value)) invalid();
	}
	for (const auto& capture : data.captures) {
		for (const auto& row : capture.worldToBox) for (float value : row) if (!std::isfinite(value)) invalid();
		if (!finite3(capture.position) || !finite3(capture.extents) || !std::isfinite(capture.radius) || capture.radius <= 0.0f ||
			std::min({capture.extents.x, capture.extents.y, capture.extents.z}) <= 0.0f || !std::isfinite(capture.intensity) ||
			capture.intensity < 0.0f || capture.shape > 1 || capture.size == 0 || capture.size > 128 ||
			capture.mipCount == 0 || capture.mipCount > 8) invalid();
		uint64_t end = capture.dataOffset;
		uint32_t size = capture.size;
		for (uint32_t mip = 0; mip < capture.mipCount; ++mip) { end += 6ull * size * size; size = std::max(size >> 1u, 1u); }
		if (end > data.reflectionPixels.size()) invalid();
	}
	std::unordered_set<UUID> receiverIDs;
	for (const auto& receiver : data.receivers) {
		if (!receiver.localFileID || !receiverIDs.insert(receiver.localFileID).second) invalid();
		for (const auto& row : receiver.worldMatrix.m) for (float value : row) if (!std::isfinite(value)) invalid();
	}
}
