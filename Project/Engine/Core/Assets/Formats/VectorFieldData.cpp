#include "VectorFieldData.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

using namespace Engine;

namespace {

	// カンマと空白を区切りとして有限値を読み込む
	bool ReadValue(std::istream& stream, double& value) {

		while (stream && (stream.peek() == ',' || std::isspace(static_cast<unsigned char>(stream.peek())))) stream.get();
		return static_cast<bool>(stream >> value) && std::isfinite(value);
	}

	// 範囲内の格子値を返す
	Vector3 ReadCell(const VectorFieldData& field, int32_t x, int32_t y, int32_t z) {

		const auto& size = field.resolution;
		return field.vectors[static_cast<size_t>(x) + static_cast<size_t>(size.x) * (y + static_cast<size_t>(size.y) * z)];
	}
}

bool Engine::ReadFGA(std::istream& stream, VectorFieldData& field, std::string& reason) {

	VectorFieldData candidate;
	double values[9]{};
	for (double& value : values) {
		if (!ReadValue(stream, value)) { reason = "FGAヘッダーが不正です"; return false; }
	}
	uint64_t count = 1;
	for (uint32_t i = 0; i < 3; ++i) {
		if (values[i] < 1 || values[i] > 256 || std::floor(values[i]) != values[i]) {
			reason = "FGAの格子数は各軸1から256です"; return false;
		}
		count *= static_cast<uint32_t>(values[i]);
		if (!(values[6 + i] > values[3 + i]) || std::abs(values[3 + i]) > std::numeric_limits<float>::max() ||
			std::abs(values[6 + i]) > std::numeric_limits<float>::max()) {
			reason = "FGAの境界が不正です"; return false;
		}
	}
	candidate.resolution = Vector3I(static_cast<int32_t>(values[0]), static_cast<int32_t>(values[1]), static_cast<int32_t>(values[2]));
	candidate.minimum = Vector3(static_cast<float>(values[3]), static_cast<float>(values[4]), static_cast<float>(values[5]));
	candidate.maximum = Vector3(static_cast<float>(values[6]), static_cast<float>(values[7]), static_cast<float>(values[8]));
	candidate.vectors.resize(static_cast<size_t>(count));
	for (auto& vector : candidate.vectors) {
		double components[3]{};
		for (double& component : components) {
			if (!ReadValue(stream, component) || std::abs(component) > std::numeric_limits<float>::max()) {
				reason = "FGAのベクトル数または値が不正です"; return false;
			}
		}
		vector = Vector3(static_cast<float>(components[0]), static_cast<float>(components[1]), static_cast<float>(components[2]));
	}
	while (stream && (stream.peek() == ',' || std::isspace(static_cast<unsigned char>(stream.peek())))) stream.get();
	if (stream.peek() != std::char_traits<char>::eof()) { reason = "FGAの末尾に余分な値があります"; return false; }
	field = std::move(candidate);
	reason.clear();
	return true;
}

Vector3 Engine::SampleVectorField(const VectorFieldData& field, const Vector3& position) {

	const auto size = field.resolution;
	if (size.x < 1 || size.y < 1 || size.z < 1 || field.vectors.size() != static_cast<size_t>(size.x) * size.y * size.z) return {};
	const auto bounds = field.maximum - field.minimum;
	if (bounds.x <= 0 || bounds.y <= 0 || bounds.z <= 0) return {};
	const auto uvw = (position - field.minimum) / bounds;
	// Vector3の大小比較は長さなので各軸の範囲を確認する
	if (!std::isfinite(uvw.x) || !std::isfinite(uvw.y) || !std::isfinite(uvw.z) ||
		uvw.x < 0 || uvw.x > 1 || uvw.y < 0 || uvw.y > 1 || uvw.z < 0 || uvw.z > 1) return {};
	const auto grid = uvw * Vector3(static_cast<float>(size.x - 1), static_cast<float>(size.y - 1), static_cast<float>(size.z - 1));
	const Vector3I low(static_cast<int32_t>(grid.x), static_cast<int32_t>(grid.y), static_cast<int32_t>(grid.z));
	const Vector3I high((std::min)(low.x + 1, size.x - 1), (std::min)(low.y + 1, size.y - 1), (std::min)(low.z + 1, size.z - 1));
	const Vector3 weight(grid.x - low.x, grid.y - low.y, grid.z - low.z);
	const auto a = Vector3::Lerp(ReadCell(field, low.x, low.y, low.z), ReadCell(field, high.x, low.y, low.z), weight.x);
	const auto b = Vector3::Lerp(ReadCell(field, low.x, high.y, low.z), ReadCell(field, high.x, high.y, low.z), weight.x);
	const auto c = Vector3::Lerp(ReadCell(field, low.x, low.y, high.z), ReadCell(field, high.x, low.y, high.z), weight.x);
	const auto d = Vector3::Lerp(ReadCell(field, low.x, high.y, high.z), ReadCell(field, high.x, high.y, high.z), weight.x);
	return Vector3::Lerp(Vector3::Lerp(a, b, weight.y), Vector3::Lerp(c, d, weight.y), weight.z);
}
