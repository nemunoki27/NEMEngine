#include "ParticleRingParametricShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Math/Math.h>

//============================================================================
//	ParticleRingParametricShape internal
//============================================================================
namespace {

	// jsonに入っているRing形状パラメータを編集する
	bool DrawRingParamsJson(const char* label, nlohmann::json& params, const char* key) {

		if (!params.contains(key) || !params[key].is_object()) {
			params[key] = Engine::PrimitiveRingParams{};
		}
		nlohmann::json& obj = params[key];
		bool changed = false;
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		changed |= Engine::ParticleGui::DragJsonFloat("外周半径", obj, "outerRadius", 1.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("内周半径", obj, "innerRadius", 0.5f, Engine::ParticleGui::MakeDragSetting(0.0f, 10000.0f));
		changed |= Engine::ParticleGui::DragJsonFloat("開始角", obj, "startAngle", 0.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f));
		changed |= Engine::ParticleGui::DragJsonFloat("終了角", obj, "endAngle", 360.0f, Engine::ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f));
		ImGui::PopID();
		return changed;
	}
}

//============================================================================
//	ParticleRingParametricShape classMethods
//============================================================================
void Engine::ParticleRingParametricShape::PackShapeParams(const nlohmann::json& params, Vector4& start, Vector4& end) const {

	PrimitiveRingParams ringStart{};
	PrimitiveRingParams ringEnd{};
	if (const auto it = params.find("ringStart"); it != params.end() && it->is_object()) { from_json(*it, ringStart); }
	if (const auto it = params.find("ringEnd"); it != params.end() && it->is_object()) { from_json(*it, ringEnd); }

	// 角度は度数法で持ちシェーダーへはラジアンで渡す
	start = Vector4(ringStart.outerRadius, ringStart.innerRadius,
		ringStart.startAngle * Math::radian, ringStart.endAngle * Math::radian);
	end = Vector4(ringEnd.outerRadius, ringEnd.innerRadius,
		ringEnd.startAngle * Math::radian, ringEnd.endAngle * Math::radian);
}

bool Engine::ParticleRingParametricShape::DrawImGui(nlohmann::json& params) const {

	bool changed = false;
	changed |= DrawRingParamsJson("開始形状", params, "ringStart");
	changed |= DrawRingParamsJson("終了形状", params, "ringEnd");
	return changed;
}

Engine::AssetID Engine::ParticleRingParametricShape::GetPipeline() const {

	return BuiltinAssets::Pipelines::ParticleRingMS;
}

int32_t Engine::ParticleRingParametricShape::GetDivide(const ParticleRenderSettings& settings) const {

	return settings.ring.divide;
}
