#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <cstring>

namespace Engine {

	ManagedNativeEntity MakeNativeEntity(ECSWorld& world, Entity entity) {
		ManagedNativeEntity native{};
		native.world = reinterpret_cast<std::uintptr_t>(&world);
		native.index = entity.index;
		native.generation = entity.generation;
		return native;
	}

	ManagedNativeEntity MakeNullNativeEntity() {
		return ManagedNativeEntity{};
	}

	ECSWorld* ResolveWorld(ManagedNativeEntity native) {
		return reinterpret_cast<ECSWorld*>(native.world);
	}

	Entity ResolveEntity(ManagedNativeEntity native) {
		return Entity{ native.index, native.generation };
	}

	int32_t CopyStringToBuffer(const std::string& str, char* buffer, int32_t capacity) {
		if (!buffer || capacity <= 0) {
			return static_cast<int32_t>(str.size());
		}
		const size_t length = std::min(str.size(), static_cast<size_t>(capacity - 1));
		std::memcpy(buffer, str.c_str(), length);
		buffer[length] = '\0';
		return static_cast<int32_t>(length);
	}

	std::string MakeSimpleTypeName(const std::string_view& fullTypeName) {
		size_t pos = fullTypeName.find_last_of('.');
		if (pos == std::string_view::npos) {
			return std::string(fullTypeName);
		}
		return std::string(fullTypeName.substr(pos + 1));
	}

	ManagedVector2 ToManagedVector2(const Vector2& value) {
		return ManagedVector2{ value.x, value.y };
	}

	ManagedVector3 ToManagedVector3(const Vector3& value) {
		return ManagedVector3{ value.x, value.y, value.z };
	}

	Vector3 ToVector3(const ManagedVector3& value) {
		return Vector3{ value.x, value.y, value.z };
	}

	ManagedQuaternion ToManagedQuaternion(const Quaternion& value) {
		return ManagedQuaternion{ value.x, value.y, value.z, value.w };
	}

	Quaternion ToQuaternion(const ManagedQuaternion& value) {
		return Quaternion{ value.x, value.y, value.z, value.w };
	}

	Vector3 MakeLocalPositionFromWorld(ECSWorld& world, const Entity& entity, const Vector3& position) {

		// 親がいなければワールド座標をそのままローカル座標として扱う
		const HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(entity);
		if (!hierarchy) {
			return position;
		}
		const Entity parent = hierarchy->parent;
		TransformComponent* parentTransform = world.TryGetComponent<TransformComponent>(parent);
		if (!parentTransform) {
			return position;
		}

		// 親のワールド行列の逆行列でワールド座標をローカル空間へ落とす
		const Matrix4x4 inverseParent = Matrix4x4::Inverse(parentTransform->worldMatrix);
		return Vector3::TransformPoint(position, inverseParent);
	}

	void MarkDirty(ECSWorld& world, const Entity& entity) {

		if (!world.TryGetComponent<TransformComponent>(entity)) {
			return;
		}
		// 自身と子孫のワールド行列を再計算対象にする
		MarkTransformSubtreeDirty(world, entity);
	}

	SceneObjectComponent& EnsureScriptSceneObject(ECSWorld& world, const Entity& entity) {
		return SceneObjectUtility::EnsureSceneObject(world, entity);
	}

	void RefreshScriptActiveTree(ECSWorld& world, const Entity& entity) {
		HierarchySystem hierarchySystem{};
		hierarchySystem.UpdateActiveInHierarchy(world, entity);
	}

} // Engine
