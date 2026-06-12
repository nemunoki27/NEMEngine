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

	// C#側とのEntityデータの変換
	ManagedNativeEntity MakeNativeEntity(ECSWorld& world, Entity entity);
	ManagedNativeEntity MakeNullNativeEntity();
	ECSWorld* ResolveWorld(ManagedNativeEntity native);
	Entity ResolveEntity(ManagedNativeEntity native);

	// 文字列のバッファコピーユーティリティ
	int32_t CopyStringToBuffer(const std::string& str, char* buffer, int32_t capacity);

	// 簡易的な型名の取得で名前空間を除去する
	std::string MakeSimpleTypeName(const std::string_view& fullTypeName);

	//--------- Managed <-> Engine型変換------------------------------------

	ManagedVector2 ToManagedVector2(const Vector2& value);
	ManagedVector3 ToManagedVector3(const Vector3& value);
	Vector3 ToVector3(const ManagedVector3& value);
	ManagedQuaternion ToManagedQuaternion(const Quaternion& value);
	Quaternion ToQuaternion(const ManagedQuaternion& value);

	//--------- Transform /階層操作の補助-----------------------------------

	// 親のワールド行列を考慮してワールド座標をローカル座標へ変換する
	Vector3 MakeLocalPositionFromWorld(ECSWorld& world, const Entity& entity, const Vector3& position);
	// Transformを再計算対象として変更済みにする
	void MarkDirty(ECSWorld& world, const Entity& entity);

	// スクリプトからのアクティブ階層の再計算で判定はSceneObjectComponent.hのIsEntityActiveInHierarchyを使う、自動追加はWorldCommandBuffer経由のためここでは既存コンポーネント前提のツリー更新だけを提供する
	void RefreshScriptActiveTree(ECSWorld& world, const Entity& entity);

} // Engine
