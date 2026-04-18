#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/MathLib/Matrix4x4.h>

namespace Engine {

	// front
	struct Entity;
	class ECSWorld;

	//============================================================================
	//	TransformComponent struct
	//============================================================================

	// トランスフォーム
	struct TransformComponent {

		// ローカルSRT
		Vector3 localPos = Vector3::AnyInit(0.0f);
		Quaternion localRotation = Quaternion::Identity();
		Vector3 localScale = Vector3::AnyInit(1.0f);

		// ワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();

		// 変更検知
		bool isDirty = true;
	};

	// json変換
	void from_json(const nlohmann::json& in, TransformComponent& component);
	void to_json(nlohmann::json& out, const TransformComponent& component);

	// helpers
	// 親子関係が変わったエンティティと、その子孫のトランスフォームを変更されたとみなす
	void MarkTransformSubtreeDirty(ECSWorld& world, const Entity& entity);

	ENGINE_REGISTER_COMPONENT(TransformComponent, "Transform");
} // Engine