#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	HierarchyComponent struct
	//============================================================================

	// 名前
	struct HierarchyComponent {

		// シリアライズ用
		UUID parentLocalFileID{};

		// ランタイム実行用
		Entity parent = Entity::Null();
		Entity firstChild = Entity::Null();
		Entity lastChild = Entity::Null();
		Entity nextSibling = Entity::Null();
		Entity prevSibling = Entity::Null();
	};

	// json変換
	void from_json(const nlohmann::json& in, HierarchyComponent& component);
	void to_json(nlohmann::json& out, const HierarchyComponent& component);

	ENGINE_REGISTER_COMPONENT(HierarchyComponent, "Hierarchy");
} // Engine