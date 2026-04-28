#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Collision/CollisionDetection.h>
#include <Engine/Core/ECS/System/Interface/ISystem.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	struct CollisionComponent;
	struct TransformComponent;

	//============================================================================
	//	CollisionSystem class
	//	衝突判定、押し戻し、OnCollisionコールバックを管理するシステム
	//============================================================================
	class CollisionSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CollisionSystem() = default;
		~CollisionSystem() override = default;

		// World終了時に接触履歴を破棄する
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		// 衝突判定、押し戻し、OnCollisionコールバックを実行する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "CollisionSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 衝突判定中に使用するEntity情報
		struct CollisionRuntimeEntity {

			Entity entity = Entity::Null();
			CollisionComponent* collision = nullptr;
			TransformComponent* transform = nullptr;
			std::vector<CollisionShapeInstance> shapes{};
		};

		//--------- variables ----------------------------------------------------

		// 前フレームに接触していた組み合わせ
		std::unordered_map<CollisionPairKey, CollisionContact, CollisionPairKeyHash> previousContacts_{};

		//--------- functions ----------------------------------------------------

		// Transformを反映した判定用形状を作成する
		CollisionShapeInstance BuildShapeInstance(const Entity& entity,
			const CollisionShape& shape, uint32_t shapeIndex, const TransformComponent& transform) const;
		// 衝突結果をもとにEntityを押し戻す
		void ApplyPushback(ECSWorld& world, CollisionRuntimeEntity& a,
			CollisionRuntimeEntity& b, const CollisionContact& contact) const;
		// OnCollisionEnterを発行する
		void DispatchCollisionEnter(ECSWorld& world, SystemContext& context, const CollisionContact& contact) const;
		// OnCollisionStayを発行する
		void DispatchCollisionStay(ECSWorld& world, SystemContext& context, const CollisionContact& contact) const;
		// OnCollisionExitを発行する
		void DispatchCollisionExit(ECSWorld& world, SystemContext& context, const CollisionContact& contact) const;
	};
} // Engine
