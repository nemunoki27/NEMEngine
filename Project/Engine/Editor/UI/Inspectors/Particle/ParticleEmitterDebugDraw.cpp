#include "ParticleEmitterDebugDraw.h"

#if defined(_DEBUG) || defined(_DEVELOPBUILD)

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCircleEmitterShape.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

	// 非等方スケールを含む行列で球の補助線を変換する
	void DrawWireSphere(Engine::LineRenderer3D& renderer, const Engine::Vector3& center,
		const Engine::Matrix4x4& matrix, float radius, bool hemisphere) {

		using namespace Engine;
		constexpr int kDivision = 16;
		constexpr float kPi = std::numbers::pi_v<float>;
		auto point = [&](float latitude, float longitude) {
			Vector3 local(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
				std::cos(latitude) * std::sin(longitude));
			return center + Vector3::Transform(local * radius, matrix);
		};
		float start = hemisphere ? 0.0f : -kPi * 0.5f;
		for (int ring = 0; ring < 8; ++ring) {
			float lat0 = std::lerp(start, kPi * 0.5f, ring / 8.0f);
			float lat1 = std::lerp(start, kPi * 0.5f, (ring + 1) / 8.0f);
			for (int i = 0; i < kDivision; ++i) {
				float angle0 = 2.0f * kPi * i / kDivision;
				float angle1 = 2.0f * kPi * (i + 1) / kDivision;
				renderer.DrawLine(point(lat0, angle0), point(lat0, angle1), Color4::Red());
				renderer.DrawLine(point(lat0, angle0), point(lat1, angle0), Color4::Red());
			}
		}
	}
}

//============================================================================
//	ParticleEmitterDebugDraw classMethods
//============================================================================
void Engine::ParticleEmitterDebugDraw::Draw(const ParticleEmitterSettings& settings,
	const Matrix4x4& emitterWorld, bool is2D) {

	// 発生offsetの中心を表示し、乱数列は消費しない
	Vector3 offset = settings.emitOffset.type == ParticleValueType::Random ?
		(settings.emitOffset.min + settings.emitOffset.max) * 0.5f : settings.emitOffset.constant;
	Vector3 center = Vector3::Transform(offset, emitterWorld);
	Matrix4x4 emitterMatrix = emitterWorld;
	emitterMatrix.m[3][0] = emitterMatrix.m[3][1] = emitterMatrix.m[3][2] = 0.0f;

	// 発生形状に対応する補助線を選ぶ
	switch (settings.shape) {
	case ParticleEmitterShape::Box: DrawBox(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Sphere: DrawSphere(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Hemisphere: DrawHemisphere(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Torus: DrawTorus(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Point: DrawPoint(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Cone: DrawCone(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Rect: DrawRect(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Cone2D: DrawCone2D(settings, center, emitterMatrix, is2D); break;
	case ParticleEmitterShape::Circle: DrawCircle(settings, center, emitterMatrix, is2D); break;
	default: break;
	}
}

void Engine::ParticleEmitterDebugDraw::DrawBox(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	// 各辺の両端を発生時と同じ行列で変換する
	Vector3 half = settings.box.size * 0.5f;
	for (uint32_t i = 0; i < 8; ++i) {
		Vector3 start((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
		for (uint32_t axis = 0; axis < 3; ++axis) {
			uint32_t bit = 1u << axis;
			if (i & bit) { continue; }
			uint32_t j = i | bit;
			Vector3 end((j & 1) ? half.x : -half.x, (j & 2) ? half.y : -half.y, (j & 4) ? half.z : -half.z);
			renderer->DrawLine(center + Vector3::Transform(start, emitterMatrix),
				center + Vector3::Transform(end, emitterMatrix), Color4::Red());
		}
	}
}

void Engine::ParticleEmitterDebugDraw::DrawSphere(const ParticleEmitterSettings& settings,
	const Vector3& center, [[maybe_unused]] const Matrix4x4& emitterMatrix, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	DrawWireSphere(*renderer, center, emitterMatrix, settings.sphere.radius, false);
}

void Engine::ParticleEmitterDebugDraw::DrawHemisphere(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	DrawWireSphere(*renderer, center, emitterMatrix, settings.sphere.radius, true);
}

void Engine::ParticleEmitterDebugDraw::DrawTorus(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	const Matrix4x4& rotationMatrix = emitterMatrix;
	const Color4 color = Color4::Red();

	// 主円周を管の内外2本の円で表す
	constexpr uint32_t kDivision = 24;
	constexpr float kStep = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kDivision);
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = kStep * static_cast<float>(i);
		const float angle1 = kStep * static_cast<float>(i + 1);
		for (const float radius : { settings.torus.radius - settings.torus.thickness,
			settings.torus.radius + settings.torus.thickness }) {

			const Vector3 p0 = center + Vector3::Transform(
				Vector3(std::cos(angle0) * radius, 0.0f, std::sin(angle0) * radius), rotationMatrix);
			const Vector3 p1 = center + Vector3::Transform(
				Vector3(std::cos(angle1) * radius, 0.0f, std::sin(angle1) * radius), rotationMatrix);
			renderer->DrawLine(p0, p1, color);
		}
	}
}

void Engine::ParticleEmitterDebugDraw::DrawPoint(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, bool is2D) {

	const Matrix4x4& rotationMatrix = emitterMatrix;
	const Color4 color = Color4::Red();
	const Vector3 direction = Vector3::NormalizeOr(settings.point.direction, Vector3(0.0f, 1.0f, 0.0f));

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// 射出方向を線で表す、スクリーン単位なので見やすい長さにする
		const Vector3 lineEnd = center + Vector3::Transform(direction * 32.0f, rotationMatrix);
		renderer2D->DrawCircle(Vector2(center.x, center.y), 4.0f, color);
		renderer2D->DrawLine(Vector2(center.x, center.y), Vector2(lineEnd.x, lineEnd.y), color);
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	// 射出方向を線で表す
	DrawWireSphere(*renderer, center, emitterMatrix, 0.05f, false);
	renderer->DrawLine(center, center + Vector3::Transform(direction, rotationMatrix), color);
}

void Engine::ParticleEmitterDebugDraw::DrawCone(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 開き角に沿った上面半径で高さ1の円錐を表す
	const float displayHeight = 1.0f;
	const float topRadius = settings.cone.radius + std::tan(settings.cone.angle * degToRad) * displayHeight;
	// 底面と上面を同じ変換で結ぶ
	constexpr uint32_t kDivision = 24;
	auto point = [&](float radius, float height, uint32_t i) {
		float angle = 2.0f * std::numbers::pi_v<float> * i / kDivision;
		return center + Vector3::Transform(Vector3(std::cos(angle) * radius, height, std::sin(angle) * radius), emitterMatrix);
	};
	for (uint32_t i = 0; i < kDivision; ++i) {
		renderer->DrawLine(point(settings.cone.radius, 0.0f, i), point(settings.cone.radius, 0.0f, i + 1), Color4::Red());
		renderer->DrawLine(point(topRadius, displayHeight, i), point(topRadius, displayHeight, i + 1), Color4::Red());
		renderer->DrawLine(point(settings.cone.radius, 0.0f, i), point(topRadius, displayHeight, i), Color4::Red());
	}
}

void Engine::ParticleEmitterDebugDraw::DrawRect(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, bool is2D) {

	const Matrix4x4& rotationMatrix = emitterMatrix;
	const Color4 color = Color4::Red();

	// 矩形の外周を線で表す
	const Vector2 half = settings.rect.size * 0.5f;
	const Vector3 corners[4] = {
		Vector3(-half.x, -half.y, 0.0f), Vector3(half.x, -half.y, 0.0f),
		Vector3(half.x, half.y, 0.0f), Vector3(-half.x, half.y, 0.0f) };

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// ローカル点をエンティティの回転と位置でスクリーン座標へ変換する
		auto toScreen = [&](const Vector3& local) {
			const Vector3 world = center + Vector3::Transform(local, rotationMatrix);
			return Vector2(world.x, world.y);
			};
		for (int32_t i = 0; i < 4; ++i) {
			renderer2D->DrawLine(toScreen(corners[i]), toScreen(corners[(i + 1) % 4]), color);
		}
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	for (int32_t i = 0; i < 4; ++i) {
		renderer->DrawLine(center + Vector3::Transform(corners[i], rotationMatrix),
			center + Vector3::Transform(corners[(i + 1) % 4], rotationMatrix), color);
	}
}

void Engine::ParticleEmitterDebugDraw::DrawCone2D(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, bool is2D) {

	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	const Matrix4x4& rotationMatrix = emitterMatrix;
	const Color4 color = Color4::Red();

	// 底辺と開き角の2本の線で扇を表す
	const float tilt = settings.cone.angle * degToRad;
	const Vector3 base0(-settings.cone.radius, 0.0f, 0.0f);
	const Vector3 base1(settings.cone.radius, 0.0f, 0.0f);

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// ローカル点をエンティティの回転と位置でスクリーン座標へ変換する
		auto toScreen = [&](const Vector3& local) {
			const Vector3 world = center + Vector3::Transform(local, rotationMatrix);
			return Vector2(world.x, world.y);
			};
		// スクリーン単位なので見やすい長さにする
		const float rayLength = (std::max)(settings.cone.radius, 32.0f);
		renderer2D->DrawLine(toScreen(base0), toScreen(base1), color);
		renderer2D->DrawLine(toScreen(base0),
			toScreen(base0 + Vector3(std::sin(-tilt), std::cos(-tilt), 0.0f) * rayLength), color);
		renderer2D->DrawLine(toScreen(base1),
			toScreen(base1 + Vector3(std::sin(tilt), std::cos(tilt), 0.0f) * rayLength), color);
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawLine(center + Vector3::Transform(base0, rotationMatrix),
		center + Vector3::Transform(base1, rotationMatrix), color);
	renderer->DrawLine(center + Vector3::Transform(base0, rotationMatrix),
		center + Vector3::Transform(base0 + Vector3(std::sin(-tilt), std::cos(-tilt), 0.0f), rotationMatrix), color);
	renderer->DrawLine(center + Vector3::Transform(base1, rotationMatrix),
		center + Vector3::Transform(base1 + Vector3(std::sin(tilt), std::cos(tilt), 0.0f), rotationMatrix), color);
}

void Engine::ParticleEmitterDebugDraw::DrawCircle(const ParticleEmitterSettings& settings,
	const Vector3& center, const Matrix4x4& emitterMatrix, bool is2D) {

	constexpr uint32_t kDivision = 24;

	const ParticleEmitterCircleParams& circle = settings.circle;
	const float span = ParticleCircleEmitterShape::GetArcSpan(circle);
	const Matrix4x4& rotationMatrix = emitterMatrix;
	const Color4 color = Color4::Red();

	// 弧の範囲だけ線を張る
	const float startAngle = Math::WrapDegree360(circle.angleMin) * Math::radian;
	const float step = span * Math::radian / static_cast<float>(kDivision);

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// ローカル点をエンティティの回転と位置でスクリーン座標へ変換する
		auto toScreen = [&](const Vector3& local) {
			const Vector3 world = center + Vector3::Transform(local, rotationMatrix);
			return Vector2(world.x, world.y);
			};
		for (uint32_t i = 0; i < kDivision; ++i) {

			const float angle0 = startAngle + step * static_cast<float>(i);
			const float angle1 = startAngle + step * static_cast<float>(i + 1);
			renderer2D->DrawLine(
				toScreen(ParticleCircleEmitterShape::GetDirection(angle0, true) * circle.radius),
				toScreen(ParticleCircleEmitterShape::GetDirection(angle1, true) * circle.radius), color);
		}
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = startAngle + step * static_cast<float>(i);
		const float angle1 = startAngle + step * static_cast<float>(i + 1);
		renderer->DrawLine(
			center + Vector3::Transform(ParticleCircleEmitterShape::GetDirection(angle0, false) * circle.radius, rotationMatrix),
			center + Vector3::Transform(ParticleCircleEmitterShape::GetDirection(angle1, false) * circle.radius, rotationMatrix), color);
	}
}

#endif
