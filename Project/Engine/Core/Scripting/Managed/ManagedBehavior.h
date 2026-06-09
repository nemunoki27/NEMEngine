#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/MonoBehavior.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>

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
		//============================================================================
		//	public Methods
		//============================================================================
		explicit ManagedBehavior(std::string typeName);
		~ManagedBehavior() override = default;

		// シリアライズフィールドを設定する
		void SetSerializedFields(const nlohmann::json& serializedFields) override;

		// C#インスタンスを生成する（未生成なら生成）。生成可否を返す
		bool EnsureInstance(ECSWorld& world, const Entity& entity) override;

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

		//--------- accessor -----------------------------------------------------

		// C#側callbackで例外が発生し、faulted状態になったか
		bool IsFaulted() const override { return faulted_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// C#側の完全修飾型名
		std::string typeName_;
		// ScriptComponentから渡されたシリアライズ値
		nlohmann::json serializedFields_ = nlohmann::json::object();
		// C#側インスタンスハンドル（世代付き。未生成はNull）
		ManagedScriptInstanceHandle managedHandle_ = ManagedScriptInstanceHandle::Null();
		// C#側callbackで例外が発生したらtrue。以降このインスタンスのcallbackは呼ばない
		bool faulted_ = false;

		//--------- functions ----------------------------------------------------

		// C#側インスタンスが未作成なら作成する
		void EnsureCreated(ECSWorld& world, const Entity& entity);
		// Invoke結果を判定し、ScriptExceptionならfaulted化して一度だけ診断ログを出す
		void HandleStatus(ManagedStatus status, const char* callbackName, const Entity& entity);
	};
} // Engine

