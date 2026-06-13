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
		ManagedBehavior(std::string scriptTypeId, std::string displayName);
		~ManagedBehavior() override = default;

		// シリアライズフィールドを設定する
		void SetSerializedFields(const nlohmann::json& serializedFields) override;

		// Play中runtime Inspector用：C#インスタンスの現在値取得/単一field即時設定
		nlohmann::json GetRuntimeSerializedState() override;
		void SetRuntimeSerializedField(const std::string& fieldId, const nlohmann::json& value) override;

		// scriptSlotIDを受け取り、CreateInstance時にC#へ転送する
		void SetSlotId(uint64_t scriptSlotId) override { scriptSlotId_ = scriptSlotId; }

		// C#インスタンスを未生成なら生成し、生成可否を返す
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

		// GetComponent<Script>でC#側インスタンスを引くためのhandleを返す、未生成はNull
		ManagedScriptInstanceHandle GetManagedHandle() const { return managedHandle_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// C#側のStable Script Type GUIDでinstance生成のキー
		std::string scriptTypeId_;
		// 表示名で診断ログ用
		std::string displayName_;
		// 所属ScriptEntryのscriptSlotID、C#のEnabled制御identityでCreateInstance時に転送する
		uint64_t scriptSlotId_ = 0;
		// ScriptComponentから渡されたシリアライズ値
		nlohmann::json serializedFields_ = nlohmann::json::object();
		// C#側インスタンスハンドルで世代付き、未生成はNull
		ManagedScriptInstanceHandle managedHandle_ = ManagedScriptInstanceHandle::Null();
		// C#側callbackで例外が発生したらtrue、以降このインスタンスのcallbackは呼ばない
		bool faulted_ = false;

		//--------- functions ----------------------------------------------------

		// C#側インスタンスが未作成なら作成する
		void EnsureCreated(ECSWorld& world, const Entity& entity);
		// Invoke結果を判定し、ScriptExceptionならfaulted化して一度だけ診断ログを出す
		void HandleStatus(ManagedStatus status, const char* callbackName, const Entity& entity);
	};
} // Engine

