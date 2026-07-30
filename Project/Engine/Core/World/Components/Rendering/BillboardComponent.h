#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>

namespace Engine {

	//============================================================================
	//	BillboardComponent struct
	//============================================================================
	// カメラ方向へ向ける回転軸設定
	struct BillboardComponent {

		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;

		// 回転を許可する軸のビット
		uint8_t axisMask = 0x07;
	};

	// json変換
	void from_json(const nlohmann::json& in, BillboardComponent& component);
	void to_json(nlohmann::json& out, const BillboardComponent& component);

	// helpers
	bool HasBillboardAxis(const BillboardComponent& component, Axis axis);
	void SetBillboardAxis(BillboardComponent& component, Axis axis, bool enabled);
	void SetBillboardAllAxes(BillboardComponent& component);
	void SanitizeBillboardAxes(BillboardComponent& component);
	bool HasAnyBillboardAxis(const BillboardComponent& component);

} // Engine
