#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	ResolvedWorldTransform struct
	//	現在のlocal値から解決したワールドTransform
	//============================================================================
	struct ResolvedWorldTransform {

		// ワールド行列
		Matrix4x4 matrix = Matrix4x4::Identity();
		// ワールド回転
		Quaternion rotation = Quaternion::Identity();
		// ワールド拡縮
		Vector3 scale = Vector3::AnyInit(1.0f);
	};

	//============================================================================
	//	TransformWorldUtility namespace
	//	LateUpdate前でも現在のlocal値からワールドTransformを解決する
	//============================================================================
	namespace TransformWorldUtility {

		// エンティティ自身のワールドTransformを解決する
		bool ResolveWorldTransform(ECSWorld& world, const Entity& entity,
			ResolvedWorldTransform& outTransform, bool includePending = false);

		// エンティティの継承設定を反映した親追従Transformを解決する
		bool ResolveParentFollowTransform(ECSWorld& world, const Entity& entity,
			ResolvedWorldTransform& outTransform, bool includePending = false);
	}
} // Engine
