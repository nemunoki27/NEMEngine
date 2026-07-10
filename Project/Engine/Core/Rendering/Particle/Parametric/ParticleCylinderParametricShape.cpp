#include "ParticleCylinderParametricShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Math/Math.h>

//============================================================================
//	ParticleCylinderParametricShape internal
//============================================================================
namespace {

	// jsonに入っているCylinder形状パラメータを編集する
	bool DrawCylinderParamsJson(const char* label, nlohmann::json& params, const char* key) {

		if (!params.contains(key) || !params[key].is_object()) {
			params[key] = Engine::PrimitiveCylinderParams{};
		}
		nlohmann::json& obj = params[key];
		// 旧スキーマのラジアン値は度数法へ移行する
		if (!obj.contains("maxAngleDegrees")) {

			Engine::PrimitiveCylinderParams migrated{};
			from_json(obj, migrated);
			obj = migrated;
		}
		bool changed = false;
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		changed |= Engine::ParticleGui::DragJsonFloat("上面半径", obj, "topRadius", 1.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("下面半径", obj, "bottomRadius", 1.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("高さ", obj, "height", 2.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("展開角", obj, "maxAngleDegrees", 360.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f));
		ImGui::PopID();
		return changed;
	}
}

//============================================================================
//	ParticleCylinderParametricShape classMethods
//============================================================================
void Engine::ParticleCylinderParametricShape::PackShapeParams(const nlohmann::json& params, Vector4& start, Vector4& end) const {

	PrimitiveCylinderParams cylinderStart{};
	PrimitiveCylinderParams cylinderEnd{};
	if (const auto it = params.find("cylinderStart"); it != params.end() && it->is_object()) { from_json(*it, cylinderStart); }
	if (const auto it = params.find("cylinderEnd"); it != params.end() && it->is_object()) { from_json(*it, cylinderEnd); }

	// 展開角は度数法で持ちシェーダーへはラジアンで渡す
	start = Vector4(cylinderStart.topRadius, cylinderStart.bottomRadius,
		cylinderStart.height, cylinderStart.maxAngle * Math::radian);
	end = Vector4(cylinderEnd.topRadius, cylinderEnd.bottomRadius,
		cylinderEnd.height, cylinderEnd.maxAngle * Math::radian);
}

bool Engine::ParticleCylinderParametricShape::DrawImGui(nlohmann::json& params) const {

	bool changed = false;
	changed |= DrawCylinderParamsJson("開始形状", params, "cylinderStart");
	changed |= DrawCylinderParamsJson("終了形状", params, "cylinderEnd");
	return changed;
}

Engine::AssetID Engine::ParticleCylinderParametricShape::GetPipeline() const {

	return BuiltinAssets::Pipelines::ParticleCylinderMS;
}

int32_t Engine::ParticleCylinderParametricShape::GetDivide(const ParticleRenderSettings& settings) const {

	return settings.cylinder.radialDivide;
}
