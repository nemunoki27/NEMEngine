#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <string>
#include <filesystem>

namespace Engine {

	// front
	struct SceneObjectComponent;

	//============================================================================
	//	ManagedScriptUtility functions
	//============================================================================

	// C#側との Entity データの変換
	ManagedNativeEntity MakeNativeEntity(ECSWorld& world, Entity entity);
	ManagedNativeEntity MakeNullNativeEntity();
	ECSWorld* ResolveWorld(ManagedNativeEntity native);
	Entity ResolveEntity(ManagedNativeEntity native);

	// 文字列のバッファコピーユーティリティ
	int32_t CopyStringToBuffer(const std::string& str, char* buffer, int32_t capacity);

	// 簡易的な型名の取得（名前空間を除去）
	std::string MakeSimpleTypeName(const std::string_view& fullTypeName);

	//--------- Managed <-> Engine 型変換 ------------------------------------

	ManagedVector2 ToManagedVector2(const Vector2& value);
	ManagedVector3 ToManagedVector3(const Vector3& value);
	Vector3 ToVector3(const ManagedVector3& value);
	ManagedQuaternion ToManagedQuaternion(const Quaternion& value);
	Quaternion ToQuaternion(const ManagedQuaternion& value);

	//--------- Transform / 階層操作の補助 -----------------------------------

	// 親のワールド行列を考慮してワールド座標をローカル座標へ変換する
	Vector3 MakeLocalPositionFromWorld(ECSWorld& world, const Entity& entity, const Vector3& position);
	// Transformを変更済み（再計算対象）にする
	void MarkDirty(ECSWorld& world, const Entity& entity);

	// スクリプトからのアクティブ階層の再計算
	// (アクティブ判定は SceneObjectComponent.h の IsEntityActiveInHierarchy を使う)
	// SceneObjectComponentの自動追加が必要な場合は WorldCommandBuffer 経由で行うため、
	// ここでは既存コンポーネントを前提としたアクティブツリー更新だけを提供する
	void RefreshScriptActiveTree(ECSWorld& world, const Entity& entity);

} // Engine
