#include "SceneGridLayout.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace Engine::SceneGridLayout;

namespace {

	struct ClipSpacePosition {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	constexpr float kEpsilon = 1e-5f;
	constexpr float kPointMergeEpsilon = 1e-3f;

	// 間隔を1・2・5の段階へ揃える
	float SnapGridStep(float rawStep) {

		rawStep = (std::max)(rawStep, 0.001f);

		float exponent = std::floor(std::log10(rawStep));
		float base = std::pow(10.0f, exponent);
		float normalized = rawStep / base;

		float snapped = 1.0f;
		if (normalized < 1.5f) {
			snapped = 1.0f;
		} else if (normalized < 3.5f) {
			snapped = 2.0f;
		} else if (normalized < 7.5f) {
			snapped = 5.0f;
		} else {
			snapped = 10.0f;
		}

		return snapped * base;
	}

	// 次の広い間隔を求める
	float NextGridStep(float step) {
		return SnapGridStep(step * 1.9f);
	}

	// 次の狭い間隔を求める
	float PrevGridStep(float step) {
		return SnapGridStep(step / 1.9f);
	}

	// 描画座標をWorldへ戻す
	Engine::Vector3 UnprojectNDC(const Engine::Matrix4x4& invViewProj, float x, float y, float z) {

		return Engine::Vector3::Transform(Engine::Vector3(x, y, z), invViewProj);
	}

	// World座標をClip座標へ変換する
	ClipSpacePosition TransformToClip(const Engine::Vector3& point, const Engine::Matrix4x4& matrix) {

		ClipSpacePosition clip{};
		clip.x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
		clip.y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
		clip.z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
		clip.w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
		return clip;
	}

	// World座標を画面へ投影する
	bool ProjectWorldToScreen(const Engine::Vector3& point, const Engine::Matrix4x4& viewProjection, uint32_t width,
		uint32_t height, Engine::Vector2& outScreen) {

		ClipSpacePosition clip = TransformToClip(point, viewProjection);
		if (std::abs(clip.w) < kEpsilon) {
			return false;
		}

		float invW = 1.0f / clip.w;
		float ndcX = clip.x * invW;
		float ndcY = clip.y * invW;

		outScreen.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
		outScreen.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height);
		return true;
	}

	// 重複しない平面頂点を追加する
	bool AddUniquePoint(std::vector<GridPoint2D>& polygon, const GridPoint2D& point) {

		for (const auto& p : polygon) {
			float dx = p.x - point.x;
			float dz = p.z - point.z;
			if ((dx * dx + dz * dz) <= (kPointMergeEpsilon * kPointMergeEpsilon)) {
				return false;
			}
		}

		polygon.push_back(point);
		return true;
	}

	// 平面頂点を中心の周囲へ並べる
	void SortPolygon(std::vector<GridPoint2D>& polygon) {

		if (polygon.size() < 3) {
			return;
		}

		float cx = 0.0f;
		float cz = 0.0f;
		for (const auto& p : polygon) {
			cx += p.x;
			cz += p.z;
		}
		cx /= static_cast<float>(polygon.size());
		cz /= static_cast<float>(polygon.size());

		std::sort(polygon.begin(), polygon.end(), [cx, cz](const GridPoint2D& a, const GridPoint2D& b) {
			float aa = std::atan2(a.z - cz, a.x - cx);
			float ab = std::atan2(b.z - cz, b.x - cx);
			return aa < ab;
		});
	}

	// 視線とグリッド平面の交点を求める
	bool IntersectGroundRay(const Engine::Vector3& cameraPos, const Engine::Vector3& nearP, const Engine::Vector3& farP,
		float planeY, float maxGroundRayDistance, GridPoint2D& outPoint) {

		Engine::Vector3 direction = farP - nearP;
		float length = direction.Length();
		if (length < kEpsilon) {
			return false;
		}
		direction /= length;

		if (std::abs(direction.y) < kEpsilon) {
			return false;
		}

		// 平行投影でも画面位置ごとに視線を分ける
		float t = (planeY - nearP.y) / direction.y;
		if (t <= 0.0f) {
			return false;
		}

		Engine::Vector3 hit = nearP + direction * t;

		// 浅い角度で極端に遠くへ飛ぶのを抑える
		Engine::Vector3 horizontal = hit - cameraPos;
		horizontal.y = 0.0f;
		float horizontalDistance = horizontal.Length();
		if (maxGroundRayDistance < horizontalDistance) {
			horizontal /= horizontalDistance;
			hit = cameraPos + horizontal * maxGroundRayDistance;
			hit.y = planeY;
		}

		outPoint.x = hit.x;
		outPoint.z = hit.z;
		return true;
	}

	// 描画座標から平面上の点を求める
	bool TryIntersectGroundFromNDC(const Engine::ResolvedCameraView& camera, const Engine::Matrix4x4& invViewProj, float ndcX,
		float ndcY, float planeY, float maxGroundRayDistance, Engine::Vector3& outPoint) {

		Engine::Vector3 nearP = UnprojectNDC(invViewProj, ndcX, ndcY, 0.0f);
		Engine::Vector3 farP = UnprojectNDC(invViewProj, ndcX, ndcY, 1.0f);

		GridPoint2D hit2D{};
		if (!IntersectGroundRay(camera.cameraPos, nearP, farP, planeY, maxGroundRayDistance, hit2D)) {
			return false;
		}

		outPoint = Engine::Vector3(hit2D.x, planeY, hit2D.z);
		return true;
	}

	// グリッド間隔の画面幅を求める
	bool EstimateProjectedStepPixels(const Engine::ResolvedCameraView& camera, uint32_t width, uint32_t height,
		const Engine::Vector3& anchor, float worldStep, float& outPixels) {

		Engine::Vector2 screenAnchor{};
		if (!ProjectWorldToScreen(anchor, camera.matrices.viewProjectionMatrix, width, height, screenAnchor)) {
			return false;
		}

		float bestPixels = 0.0f;
		bool success = false;

		const std::array<Engine::Vector3, 2> offsets = {
			Engine::Vector3(worldStep, 0.0f, 0.0f),
			Engine::Vector3(0.0f, 0.0f, worldStep),
		};

		for (const auto& offset : offsets) {
			Engine::Vector2 screenOther{};
			if (!ProjectWorldToScreen(anchor + offset, camera.matrices.viewProjectionMatrix, width, height, screenOther)) {
				continue;
			}

			float dx = screenOther.x - screenAnchor.x;
			float dy = screenOther.y - screenAnchor.y;
			float pixels = std::sqrt(dx * dx + dy * dy);
			bestPixels = (std::max)(bestPixels, pixels);
			success = true;
		}

		outPixels = bestPixels;
		return success;
	}

	// 間隔計算の基準点を選ぶ
	Engine::Vector3 ChooseGridAnchor(const Engine::ResolvedCameraView& camera, const std::vector<GridPoint2D>& polygon,
		float planeY, float maxGroundRayDistance) {

		const Engine::Matrix4x4 invVP = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;

		const std::array<Engine::Vector2, 6> samples = {
			Engine::Vector2(0.0f, -0.80f),
			Engine::Vector2(0.0f, -0.60f),
			Engine::Vector2(0.0f, -0.40f),
			Engine::Vector2(-0.35f, -0.75f),
			Engine::Vector2(0.35f, -0.75f),
			Engine::Vector2(0.0f, 0.00f),
		};

		for (const auto& sample : samples) {
			Engine::Vector3 point{};
			if (TryIntersectGroundFromNDC(camera, invVP, sample.x, sample.y, planeY, maxGroundRayDistance, point)) {
				return point;
			}
		}

		if (!polygon.empty()) {
			float bestDistance = (std::numeric_limits<float>::max)();
			Engine::Vector3 bestPoint(polygon.front().x, planeY, polygon.front().z);
			for (const auto& p : polygon) {
				Engine::Vector3 candidate(p.x, planeY, p.z);
				float distance = DistanceXZ(candidate, camera.cameraPos);
				if (distance < bestDistance) {
					bestDistance = distance;
					bestPoint = candidate;
				}
			}
			return bestPoint;
		}

		return Engine::Vector3(camera.cameraPos.x, planeY, camera.cameraPos.z);
	}
}

// 画面の外周から表示範囲を求める
bool Engine::SceneGridLayout::BuildVisibleGroundPolygon(const Engine::ResolvedCameraView& camera, int samplesPerEdge,
	float planeY, float maxGroundRayDistance, std::vector<GridPoint2D>& outPolygon) {

	outPolygon.clear();

	const Engine::Matrix4x4 invVP = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;
	const int sampleCount = (std::max)(samplesPerEdge, 4);

	auto addSample = [&](float ndcX, float ndcY) {
		Engine::Vector3 nearP = UnprojectNDC(invVP, ndcX, ndcY, 0.0f);
		Engine::Vector3 farP = UnprojectNDC(invVP, ndcX, ndcY, 1.0f);

		GridPoint2D hit{};
		if (IntersectGroundRay(camera.cameraPos, nearP, farP, planeY, maxGroundRayDistance, hit)) {
			AddUniquePoint(outPolygon, hit);
		}
	};

	// 下辺
	for (int i = 0; i < sampleCount; ++i) {
		float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
		addSample(Math::Lerp(-1.0f, 1.0f, t), -1.0f);
	}
	// 右辺
	for (int i = 1; i < sampleCount - 1; ++i) {
		float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
		addSample(1.0f, Math::Lerp(-1.0f, 1.0f, t));
	}
	// 上辺
	for (int i = sampleCount - 1; i >= 0; --i) {
		float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
		addSample(Math::Lerp(-1.0f, 1.0f, t), 1.0f);
	}
	// 左辺
	for (int i = sampleCount - 2; i >= 1; --i) {
		float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
		addSample(-1.0f, Math::Lerp(-1.0f, 1.0f, t));
	}

	if (outPolygon.size() < 3) {
		return false;
	}

	SortPolygon(outPolygon);
	return true;
}

// 平面上の距離を求める
float Engine::SceneGridLayout::DistanceXZ(const Engine::Vector3& a, const Engine::Vector3& b) {

	float dx = a.x - b.x;
	float dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

// 画面幅に合わせて間隔を補間する
Engine::SceneGridLayout::GridStepBlend Engine::SceneGridLayout::DetermineMinorStepBlend(
	const Engine::ResolvedCameraView& camera, uint32_t width, uint32_t height, const std::vector<GridPoint2D>& polygon,
	float planeY, float maxGroundRayDistance, float baseHeightDivisor, float baseMinStep, float targetPixelMin,
	float targetPixelMax) {

	Engine::Vector3 anchor = ChooseGridAnchor(camera, polygon, planeY, maxGroundRayDistance);

	float safeBaseHeightDivisor = (std::max)(baseHeightDivisor, 0.001f);
	float safeBaseMinStep = (std::max)(baseMinStep, 0.001f);
	float safeTargetPixelMin = (std::max)(targetPixelMin, 1.0f);
	float safeTargetPixelMax = (std::max)(targetPixelMax, safeTargetPixelMin + 1.0f);

	// World単位あたりの画面幅を求める
	float pixelsPerUnit = 0.0f;
	if (!EstimateProjectedStepPixels(camera, width, height, anchor, 1.0f, pixelsPerUnit) || pixelsPerUnit <= kEpsilon) {

		float fallback =
			SnapGridStep((std::max)(std::abs(camera.cameraPos.y - planeY) / safeBaseHeightDivisor, safeBaseMinStep));

		GridStepBlend result{};
		result.minorStep0 = fallback;
		result.minorStep1 = NextGridStep(fallback);
		result.blend = 0.0f;
		return result;
	}

	// 目標幅の対数中心を求める
	float targetPixelCenter = std::sqrt(safeTargetPixelMin * safeTargetPixelMax);

	// 目標幅から細線の間隔を求める
	float idealMinorStep = (std::max)(targetPixelCenter / pixelsPerUnit, safeBaseMinStep);

	// 目標間隔を挟む2段階を選ぶ
	float upper = SnapGridStep(idealMinorStep);
	if (upper < idealMinorStep) {
		upper = NextGridStep(upper);
	}

	float lower = PrevGridStep(upper);
	lower = (std::max)(lower, safeBaseMinStep);

	// 上下の間隔が等しければ補間しない
	if (std::abs(upper - lower) < kEpsilon) {
		GridStepBlend result{};
		result.minorStep0 = lower;
		result.minorStep1 = upper;
		result.blend = 0.0f;
		return result;
	}

	// 段階間の補間率を対数で求める
	float denom = std::log(upper / lower);
	float blend = 0.0f;
	if (std::abs(denom) > kEpsilon) {
		blend = std::log(idealMinorStep / lower) / denom;
	}
	blend = Math::Saturate(blend);

	// 補間率の切り替わりを滑らかにする
	blend = blend * blend * (3.0f - 2.0f * blend);

	GridStepBlend result{};
	result.minorStep0 = lower;
	result.minorStep1 = upper;
	result.blend = blend;
	return result;
}
