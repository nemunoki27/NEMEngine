#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Matrix4x4.h>

namespace Engine {

	//============================================================================
	//	UVTransformComponent struct
	//============================================================================

	// テクスチャのUVを変換
	struct UVTransformComponent {
		
		// SRT
		Vector2 pos = Vector2::AnyInit(0.0f);
		float rotation = 0.0f;
		Vector2 scale = Vector2::AnyInit(1.0f);

		// 前フレームのSRT
		Vector2 prePos = Vector2::AnyInit(0.0f);
		float preRotation = 0.0f;
		Vector2 preScale = Vector2::AnyInit(1.0f);

		// ワールド行列
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};

	// json変換
	void from_json(const nlohmann::json& in, UVTransformComponent& component);
	void to_json(nlohmann::json& out, const UVTransformComponent& component);

	ENGINE_REGISTER_COMPONENT(UVTransformComponent, "UVTransform");
} // Engine