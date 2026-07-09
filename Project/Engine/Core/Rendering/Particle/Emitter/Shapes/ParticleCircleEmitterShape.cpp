#include "ParticleCircleEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cmath>
#include <numbers>

//============================================================================
//	ParticleCircleEmitterShape classMethods
//============================================================================
void Engine::ParticleCircleEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.circle.radius = data.value("circleRadius", settings.circle.radius);
	settings.circle.arc = data.value("circleArc", settings.circle.arc);
}

void Engine::ParticleCircleEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["circleRadius"] = settings.circle.radius;
	data["circleArc"] = settings.circle.arc;
}

void Engine::ParticleCircleEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, bool is2D) const {

	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 円弧上から外向きに飛ばす、3DはXZ平面で2DはXY平面
	const float angle = RandomGenerator::Generate(0.0f, settings.circle.arc * degToRad);
	direction = is2D ?
		Vector3(std::cos(angle), std::sin(angle), 0.0f) :
		Vector3(std::cos(angle), 0.0f, std::sin(angle));
	position = direction * settings.circle.radius;
}

void Engine::ParticleCircleEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) const {

	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
	constexpr uint32_t kDivision = 24;

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Color4 color = Color4::Red();
	const float arc = settings.circle.arc * degToRad;
	const float step = arc / static_cast<float>(kDivision);

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

			const float angle0 = step * static_cast<float>(i);
			const float angle1 = step * static_cast<float>(i + 1);
			renderer2D->DrawLine(
				toScreen(Vector3(std::cos(angle0), std::sin(angle0), 0.0f) * settings.circle.radius),
				toScreen(Vector3(std::cos(angle1), std::sin(angle1), 0.0f) * settings.circle.radius), color);
		}
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	// 円弧の範囲だけ線を張る
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = step * static_cast<float>(i);
		const float angle1 = step * static_cast<float>(i + 1);
		const Vector3 local0 = Vector3(std::cos(angle0), 0.0f, std::sin(angle0));
		const Vector3 local1 = Vector3(std::cos(angle1), 0.0f, std::sin(angle1));
		renderer->DrawLine(center + Vector3::Transform(local0 * settings.circle.radius, rotationMatrix),
			center + Vector3::Transform(local1 * settings.circle.radius, rotationMatrix), color);
	}
}

bool Engine::ParticleCircleEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("半径", settings.circle.radius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("円弧角度", settings.circle.arc, ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	return changed;
}
