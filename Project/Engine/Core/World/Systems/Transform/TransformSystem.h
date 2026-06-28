#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	TransformSystem class
	//	トランスフォームの更新、管理を行うシステム
	//============================================================================
	class TransformSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TransformSystem() = default;
		~TransformSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "TransformSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// トランスフォーム更新のためのスタックノード
		struct StackNode {

			Entity entity = Entity::Null();
			bool parentDirty = false;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Entity> roots_;
		std::vector<StackNode> stack_;
	};
} // Engine
