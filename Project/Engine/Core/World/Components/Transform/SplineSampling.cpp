#include "SplineComponent.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	// 制御多角形の長さで直線近似の誤差を抑える
	void Subdivide(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float tolerance, uint32_t depth,
		std::vector<SplineSample>& samples) {

		const float polygon = (b - a).Length() + (c - b).Length() + (d - c).Length();
		if (depth >= 12 || polygon - (d - a).Length() <= tolerance) {
			const auto& previous = samples.back();
			samples.push_back({d, previous.distance + (d - previous.position).Length(), Vector3::NormalizeOr(d - c, previous.tangent)});
			return;
		}
		const auto ab = (a + b) * 0.5f;
		const auto bc = (b + c) * 0.5f;
		const auto cd = (c + d) * 0.5f;
		const auto abc = (ab + bc) * 0.5f;
		const auto bcd = (bc + cd) * 0.5f;
		const auto center = (abc + bcd) * 0.5f;
		Subdivide(a, ab, abc, center, tolerance * 0.5f, depth + 1, samples);
		Subdivide(center, bcd, cd, d, tolerance * 0.5f, depth + 1, samples);
	}
}

std::vector<SplineSample> Engine::BuildSplineSamples(const SplineData& data, const Matrix4x4& world) {

	std::vector<SplineSample> samples;
	if (data.points.size() < 2) return samples;
	const auto& first = data.points.front();
	samples.push_back({Vector3::Transform(first.position, world), 0,
		Vector3::NormalizeOr(Vector3::TransferNormal(first.tangentOut, world), Vector3(1, 0, 0))});
	const size_t count = data.closed ? data.points.size() : data.points.size() - 1;
	for (size_t i = 0; i < count; ++i) {
		const auto& a = data.points[i];
		const auto& b = data.points[(i + 1) % data.points.size()];
		Subdivide(Vector3::Transform(a.position, world), Vector3::Transform(a.position + a.tangentOut, world),
			Vector3::Transform(b.position + b.tangentIn, world), Vector3::Transform(b.position, world),
			(std::max)(data.tolerance, 0.00001f), 0, samples);
	}
	return samples;
}

SplineSample Engine::EvaluateSplineDistance(std::span<const SplineSample> samples, float distance, bool loop) {

	if (samples.empty()) return {};
	const float length = samples.back().distance;
	if (length <= 0 || !std::isfinite(distance)) return samples.front();
	distance = loop ? std::fmod(distance, length) : std::clamp(distance, 0.0f, length);
	if (distance < 0) distance += length;
	auto right = std::lower_bound(samples.begin(), samples.end(), distance,
		[](const SplineSample& sample, float value) { return sample.distance < value; });
	if (right == samples.begin()) return *right;
	if (right == samples.end()) return samples.back();
	const auto& left = *(right - 1);
	const float weight = (distance - left.distance) / (std::max)(right->distance - left.distance, 0.000001f);
	return {Vector3::Lerp(left.position, right->position, weight), distance,
		Vector3::NormalizeOr(Vector3::Lerp(left.tangent, right->tangent, weight), left.tangent)};
}
