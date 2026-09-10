#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionDetection.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	struct CollisionComponent;
	struct CollisionRuntimeStateComponent;
	struct TransformComponent;

	//============================================================================
	//	CollisionSystem class
	//	衝突判定、押し戻し、OnCollisionコールバックを管理するシステム
	//============================================================================
	class CollisionSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CollisionSystem() = default;
		~CollisionSystem() override = default;

		// World終了時に接触履歴を破棄する
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		// 固定ステップで衝突判定、押し戻し、OnCollisionコールバックを実行する
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		// Edit中の衝突表示を更新する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "CollisionSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 衝突判定中に使用するEntity情報
		struct CollisionRuntimeEntity {

			Entity entity = Entity::Null();
			CollisionComponent* collision = nullptr;
			CollisionRuntimeStateComponent* state = nullptr;
			TransformComponent* transform = nullptr;
			CollisionShapeInstance shape{};
			bool hasShape = false;
			bool dynamicBody = false;
			bool surfaceBox = false;
			uint8_t internalFaces = 0;
		};

		//--------- variables ----------------------------------------------------

		// 前フレームに接触していた組み合わせ
		std::unordered_map<CollisionPairKey, CollisionContact, CollisionPairKeyHash> previousContacts_{};

		//--------- functions ----------------------------------------------------

		// 衝突判定を実行し、必要なら押し戻しとコールバックを処理する
		void UpdateCollisions(ECSWorld& world, SystemContext& context, bool applyResponse);
		// Transform変更後の判定形状を現在位置から再構築する
		void RebuildRuntimeShape(ECSWorld& world,
			CollisionRuntimeEntity& runtime) const;
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

