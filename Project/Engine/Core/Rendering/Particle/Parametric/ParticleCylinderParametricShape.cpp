#include "ParticleCylinderParametricShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <numbers>

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
		bool changed = false;
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		changed |= Engine::ParticleGui::DragJsonFloat("上面半径", obj, "topRadius", 1.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("下面半径", obj, "bottomRadius", 1.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("高さ", obj, "height", 2.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("展開角", obj, "maxAngle", std::numbers::pi_v<float> *2.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10.0f));
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

	start = Vector4(cylinderStart.topRadius, cylinderStart.bottomRadius,
		cylinderStart.height, cylinderStart.maxAngle);
	end = Vector4(cylinderEnd.topRadius, cylinderEnd.bottomRadius,
		cylinderEnd.height, cylinderEnd.maxAngle);
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
