#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/MonoBehavior.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	ManagedBehavior class
	//	C# ScriptBehaviourをMonoBehaviorのライフサイクルへ接続する
	//============================================================================
	class ManagedBehavior :
		public MonoBehavior {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ManagedBehavior(std::string typeName);
		~ManagedBehavior() override = default;

		// シリアライズフィールドを設定する
		void SetSerializedFields(const nlohmann::json& serializedFields) override;

		// ライフサイクル
		void Awake(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void Start(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void OnEnable(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void OnDisable(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void OnDestroy(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void FixedUpdate(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void Update(ECSWorld& world, const SystemContext& context, const Entity& entity) override;
		void LateUpdate(ECSWorld& world, const SystemContext& context, const Entity& entity) override;

		// 衝突イベント
		void OnCollisionEnter(ECSWorld& world, const SystemContext& context, const CollisionContact& collision) override;
		void OnCollisionStay(ECSWorld& world, const SystemContext& context, const CollisionContact& collision) override;
		void OnCollisionExit(ECSWorld& world, const SystemContext& context, const CollisionContact& collision) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// C#側の完全修飾型名
		std::string typeName_;
		// ScriptComponentから渡されたシリアライズ値
		nlohmann::json serializedFields_ = nlohmann::json::object();
		// C#側インスタンスハンドル
		int32_t managedHandle_ = 0;

		//--------- functions ----------------------------------------------------

		// C#側インスタンスが未作成なら作成する
		void EnsureCreated(ECSWorld& world, const Entity& entity);
	};
} // Engine
