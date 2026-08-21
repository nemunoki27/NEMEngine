#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

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

		// エディター上で扱う次元
		Dimension dimension = Dimension::Type3D;

		// ワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();

		// 親追従の継承設定、座標は常に追従し回転とスケールを任意で無視できる、エンティティ親もジョイント親も同じ扱い
		bool ignoreParentScale = false;
		bool ignoreParentRotation = false;

		// 変更検知
		bool isDirty = true;
	};

	// json変換
	void from_json(const nlohmann::json& in, TransformComponent& component);
	void to_json(nlohmann::json& out, const TransformComponent& component);

	// helpers
	// トランスフォームコンポーネントからローカル行列を作る
	inline Matrix4x4 MakeLocalMatrix(const TransformComponent& transform) {
		return Matrix4x4::MakeAffineMatrix(transform.localScale, transform.localRotation, transform.localPos);
	}

	// 親子関係が変わったエンティティと、その子孫のトランスフォームを変更されたとみなす
	void MarkTransformSubtreeDirty(ECSWorld& world, const Entity& entity);

} // Engine
