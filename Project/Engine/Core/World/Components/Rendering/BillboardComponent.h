#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	BillboardComponent struct
	//============================================================================

	// カメラ方向へ向ける回転軸設定
	struct BillboardComponent {

		std::vector<Axis> axes{ Axis::X, Axis::Y, Axis::Z };
	};

	// json変換
	void from_json(const nlohmann::json& in, BillboardComponent& component);
	void to_json(nlohmann::json& out, const BillboardComponent& component);

	// helpers
	bool HasBillboardAxis(const BillboardComponent& component, Axis axis);
	void SetBillboardAxis(BillboardComponent& component, Axis axis, bool enabled);
	void SetBillboardAllAxes(BillboardComponent& component);
	void SanitizeBillboardAxes(BillboardComponent& component);

	ENGINE_REGISTER_COMPONENT(BillboardComponent, "Billboard");
} // Engine
