#include "ParticlePointEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

//============================================================================
//	ParticlePointEmitterShape classMethods
//============================================================================
void Engine::ParticlePointEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	if (const auto it = data.find("pointDirection"); it != data.end()) { settings.point.direction = Vector3::FromJson(*it); }
}

void Engine::ParticlePointEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["pointDirection"] = settings.point.direction.ToJson();
}

void Engine::ParticlePointEmitterShape::InitParticle([[maybe_unused]] Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// 原点から指定方向へ飛ばす
	direction = Vector3::NormalizeOr(settings.point.direction, Vector3(0.0f, 1.0f, 0.0f));
}

void Engine::ParticlePointEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

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
#endif
}
