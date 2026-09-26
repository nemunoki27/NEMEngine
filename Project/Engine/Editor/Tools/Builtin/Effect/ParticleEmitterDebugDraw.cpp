#include "ParticleEmitterDebugDraw.h"

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

//============================================================================
//	ParticleEmitterDebugDraw classMethods
//============================================================================
void Engine::ParticleEmitterDebugDraw::Draw(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) {

	// 発生形状に対応する補助線を選ぶ
	switch (settings.shape) {
	case ParticleEmitterShape::Box: DrawBox(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Sphere: DrawSphere(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Hemisphere: DrawHemisphere(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Torus: DrawTorus(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Point: DrawPoint(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Cone: DrawCone(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Rect: DrawRect(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Cone2D: DrawCone2D(settings, center, rotation, is2D); break;
	case ParticleEmitterShape::Circle: DrawCircle(settings, center, rotation, is2D); break;
	default: break;
	}
}

void Engine::ParticleEmitterDebugDraw::DrawBox(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawOBB(center, settings.box.size * 0.5f, rotation, Color4::Red());
}

void Engine::ParticleEmitterDebugDraw::DrawSphere(const ParticleEmitterSettings& settings,
	const Vector3& center, [[maybe_unused]] const Quaternion& rotation, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawSphere(center, settings.sphere.radius, Color4::Red(), 1.0f);
}

void Engine::ParticleEmitterDebugDraw::DrawHemisphere(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawHemisphere(center, settings.sphere.radius, rotation, Color4::Red());
}

void Engine::ParticleEmitterDebugDraw::DrawTorus(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
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
	const Vector3& center, const Quaternion& rotation, bool is2D) {

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
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
	renderer->DrawSphere(center, 0.05f, color, 1.0f);
	renderer->DrawLine(center, center + Vector3::Transform(direction, rotationMatrix), color);
}

void Engine::ParticleEmitterDebugDraw::DrawCone(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 開き角に沿った上面半径で高さ1の円錐を表す
	const float displayHeight = 1.0f;
	const float topRadius = settings.cone.radius + std::tan(settings.cone.angle * degToRad) * displayHeight;
	renderer->DrawCone(center, settings.cone.radius, topRadius, displayHeight, rotation, Color4::Red());
}

void Engine::ParticleEmitterDebugDraw::DrawRect(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) {

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
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
	const Vector3& center, const Quaternion& rotation, bool is2D) {

	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
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
	const Vector3& center, const Quaternion& rotation, bool is2D) {

	constexpr uint32_t kDivision = 24;

	const ParticleEmitterCircleParams& circle = settings.circle;
	const float span = ParticleCircleEmitterShape::GetArcSpan(circle);
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
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
