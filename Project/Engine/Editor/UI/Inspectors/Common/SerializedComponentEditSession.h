#pragma once

//============================================================================
//	include
//============================================================================
#include "InspectorDrawerCommon.h"
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

namespace Engine {

	// 型固有の保存とプレビューへの接続
	template<typename T>
	class SerializedComponentEditHooks {
	public:
		virtual ~SerializedComponentEditHooks() = default;
		virtual void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const T& component) = 0;
		virtual void OnBeforeCommit(const T& beforeComponent, T& afterComponent) = 0;
		virtual void SerializeDraft(ECSWorld& world, const Entity& entity, const T& component, nlohmann::json& out) const = 0;
		virtual void ApplyPreview(ECSWorld& world, const Entity& entity, const T& previewComponent) = 0;
		virtual void OnAfterPreviewApplied(ECSWorld& world, const Entity& entity, const T& previewComponent) = 0;
	};

	//============================================================================
	//	SerializedComponentEditSession class
	//	コンポーネントの編集値と確定前の状態を所有する
	//============================================================================
	template<typename T>
	class SerializedComponentEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SerializedComponentEditSession(std::string_view componentTypeName) : componentTypeName_(componentTypeName) {}
		~SerializedComponentEditSession() { RestorePreview(); }

		// ワールドから編集値を同期する
		void SyncDraftFromWorld(ECSWorld& world, const Entity& entity, SerializedComponentEditHooks<T>& hooks);
		// 要求された編集を確定する
		void CommitIfNeeded(const EditorPanelContext& context,
			ECSWorld& world, const Entity& entity, SerializedComponentEditHooks<T>& hooks);
		// 要求されたプレビューを適用する
		void ApplyPreviewIfNeeded(ECSWorld& world, const Entity& entity, SerializedComponentEditHooks<T>& hooks);
		// 入力結果を編集要求へ反映する
		void PushEditResult(const ValueEditResult& result, bool& anyItemActive);

		//--------- accessor -----------------------------------------------------

		bool NeedsSync(const ECSWorld& world, UUID entityUUID) const {
			const auto editingWorld = editingWorld_.lock();
			return !editingWorld || editingWorld != world.GetLifetime() ||
				editingEntityStableUUID_ != entityUUID || !isEditing_;
		}
		void SetEditing(bool editing) { isEditing_ = editing; }
		void RequestCommit() { commitRequested_ = true; }
		T& GetDraft() { return draftComponent_; }
		const T& GetDraft() const { return draftComponent_; }
		const std::string& GetComponentTypeName() const { return componentTypeName_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::string componentTypeName_;
		// 編集中のエンティティのUUID
		UUID editingEntityStableUUID_{};
		std::weak_ptr<const ECSWorldLifetime> editingWorld_;
		ECSWorld* editingWorldPointer_ = nullptr;
		T draftComponent_{};

		// 編集中か
		bool isEditing_ = false;
		// コミットが必要な状態か
		bool commitRequested_ = false;

		// プレビューがアクティブか
		bool previewActive_ = false;
		bool previewRequested_ = false;
		// プレビュー開始時のコンポーネントの状態
		T previewBeginComponent_{};
		nlohmann::json previewBeginData_{};

		// 編集対象を失う前に未確定プレビューを戻す
		void RestorePreview();

	};

	template<typename T>
	inline void SerializedComponentEditSession<T>::SyncDraftFromWorld(ECSWorld& world, const Entity& entity, SerializedComponentEditHooks<T>& hooks) {

		// エンティティが存在しない、またはコンポーネントがない場合は何もしない
		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}
		const auto editingWorld = editingWorld_.lock();
		if (editingWorldPointer_ != &world || !editingWorld || editingEntityStableUUID_ != world.GetUUID(entity)) {
			RestorePreview();
		}

		// ワールドからドラフトを更新
		draftComponent_ = world.GetComponent<T>(entity);
		editingEntityStableUUID_ = world.GetUUID(entity);
		editingWorld_ = world.GetLifetime();
		editingWorldPointer_ = &world;

		// ドラフトをワールドから同期したのでプレビュー状態をリセット
		previewActive_ = false;
		previewRequested_ = false;
		previewBeginComponent_ = T{};
		previewBeginData_ = nlohmann::json{};

		// ドラフトをワールドから同期した後の追加処理
		hooks.OnSyncDraftFromWorld(world, entity, draftComponent_);
	}

	template<typename T>
	inline void SerializedComponentEditSession<T>::CommitIfNeeded(const EditorPanelContext& context,
		ECSWorld& world, const Entity& entity, SerializedComponentEditHooks<T>& hooks) {

		if (!commitRequested_) {
			return;
		}
		commitRequested_ = false;

		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}

		const T beforeComponent = previewActive_ ? previewBeginComponent_ : world.GetComponent<T>(entity);

		T afterComponent = draftComponent_;
		hooks.OnBeforeCommit(beforeComponent, afterComponent);

		nlohmann::json beforeData;
		if (previewActive_) {
			beforeData = previewBeginData_;
		} else if (!world.SerializeComponentToJson(entity, componentTypeName_, beforeData)) {
			return;
		}
		nlohmann::json afterData;
		hooks.SerializeDraft(world, entity, afterComponent, afterData);

		// シリアライズ後のデータが同じならコミットしない
		if (beforeData == afterData) {
			previewActive_ = false;
			SyncDraftFromWorld(world, entity, hooks);
			return;
		}
		if (context.IsPlaying()) {
			// Play中はプレビュー済みの実行値を確定し、Undoへは記録しない
			previewActive_ = false;
			SyncDraftFromWorld(world, entity, hooks);
			return;
		}

		// コマンドを実行して変更をコミット
		context.host->ExecuteEditorCommand(std::make_unique<SetSerializedComponentCommand>(
			entity, componentTypeName_, beforeData, afterData));

		previewActive_ = false;
		SyncDraftFromWorld(world, entity, hooks);
	}

	template<typename T>
	inline void SerializedComponentEditSession<T>::ApplyPreviewIfNeeded(ECSWorld& world, const Entity& entity, SerializedComponentEditHooks<T>& hooks) {

		if (!previewRequested_) {
			return;
		}
		previewRequested_ = false;

		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}

		if (!previewActive_) {
			previewBeginComponent_ = world.GetComponent<T>(entity);
			if (!world.SerializeComponentToJson(entity, componentTypeName_, previewBeginData_)) {
				return;
			}
			previewActive_ = true;
		}

		T previewComponent = draftComponent_;
		hooks.OnBeforeCommit(previewBeginComponent_, previewComponent);

		// プレビューを適用
		hooks.ApplyPreview(world, entity, previewComponent);
		hooks.OnAfterPreviewApplied(world, entity, previewComponent);

		// プレビュー適用後のワールド状態へドラフトを再同期
		if (world.IsAlive(entity) && world.HasComponent<T>(entity)) {

			draftComponent_ = world.GetComponent<T>(entity);
			hooks.OnSyncDraftFromWorld(world, entity, draftComponent_);
		}
	}

	template<typename T>
	inline void SerializedComponentEditSession<T>::PushEditResult(const ValueEditResult& result, bool& anyItemActive) {

		InspectorDrawerCommon::AccumulateEditResult(result, anyItemActive, commitRequested_);

		if (result.valueChanged) {
			previewRequested_ = true;
		}
	}

	template<typename T>
	inline void SerializedComponentEditSession<T>::RestorePreview() {

		const auto lifetime = editingWorld_.lock();
		if (previewActive_ && lifetime && lifetime->IsAlive() && editingWorldPointer_) {
			const Entity entity = editingWorldPointer_->FindByUUID(editingEntityStableUUID_);
			if (editingWorldPointer_->IsAlive(entity) && editingWorldPointer_->HasComponent<T>(entity)) {
				// World切替やPanel破棄では未確定値を残さない
				editingWorldPointer_->ApplyComponentJson(entity, componentTypeName_, previewBeginData_);
			}
		}
		previewActive_ = false;
		previewRequested_ = false;
		previewBeginData_ = nlohmann::json{};
		editingWorld_.reset();
		editingWorldPointer_ = nullptr;
		editingEntityStableUUID_ = {};
	}
}
