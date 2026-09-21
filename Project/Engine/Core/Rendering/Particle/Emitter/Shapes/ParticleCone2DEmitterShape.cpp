#include "ParticleCone2DEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

//============================================================================
//	ParticleCone2DEmitterShape classMethods
//============================================================================
void Engine::ParticleCone2DEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.cone.angle = data.value("coneAngle", settings.cone.angle);
	settings.cone.radius = data.value("coneRadius", settings.cone.radius);
}

void Engine::ParticleCone2DEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["coneAngle"] = settings.cone.angle;
	data["coneRadius"] = settings.cone.radius;
}

void Engine::ParticleCone2DEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 底辺の線分から開き角の範囲で上向きに飛ばす
	position = Vector3(RandomGenerator::Generate(-settings.cone.radius, settings.cone.radius), 0.0f, 0.0f);
	const float tilt = RandomGenerator::Generate(
		-settings.cone.angle * degToRad, settings.cone.angle * degToRad);
	direction = Vector3(std::sin(tilt), std::cos(tilt), 0.0f);
}

void Engine::ParticleCone2DEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

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
#endif
}

bool Engine::ParticleCone2DEmitterShape::DrawImGui([[maybe_unused]] ParticleEmitterSettings& settings) const {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= MyGUI::DragFloat("開き角", settings.cone.angle, ParticleGui::MakeDragSetting(0.0f, 89.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("底辺半径", settings.cone.radius, ParticleGui::MakeDragSetting(0.0f, 100000.0f)).valueChanged;
	return changed;
#else
	return false;
#endif
}
