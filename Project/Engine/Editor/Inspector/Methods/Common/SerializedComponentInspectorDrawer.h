#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Interface/IInspectorComponentDrawer.h>
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/Command/Methods/SetSerializedComponentCommand.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/Utility/ImGui/MyGUI.h>

// c++
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <filesystem>

namespace Engine {

	//============================================================================
	//	SerializedComponentInspectorDrawer class
	//	シリアライズ可能なコンポーネントのインスペクター描画
	//============================================================================
	template <typename T>
	class SerializedComponentInspectorDrawer :
		public IInspectorComponentDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SerializedComponentInspectorDrawer(const std::string_view& headerLabel, const std::string_view& componentTypeName) :
			headerLabel_(headerLabel), componentTypeName_(componentTypeName) {}
		~SerializedComponentInspectorDrawer() = default;

		// インスペクター描画
		void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) override;

		//--------- accessor -----------------------------------------------------

		// 描画可能か
		bool CanDraw(ECSWorld& world, const Entity& entity) const override;
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// コミット状態へ反映する
		void PushEditResult(const ValueEditResult& result, bool& anyItemActive);

		// フィールド描画
		template <typename DrawFunc>
		void DrawField(bool& anyItemActive, DrawFunc&& drawFunc);
		// 派生先のクラスでフィールド描画を実装するための純粋仮想関数
		virtual void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) = 0;

		// ワールドからドラフト同期直後の追加処理
		virtual void OnSyncDraftFromWorld([[maybe_unused]] const T& component) {}
		// コミット前の追加処理
		virtual void OnBeforeCommit([[maybe_unused]] const T& beforeComponent, [[maybe_unused]] T& afterComponent) {}

		// プレビューの適用
		virtual void ApplyPreview(ECSWorld& world, const Entity& entity, const T& previewComponent);
		virtual void OnAfterPreviewApplied([[maybe_unused]] ECSWorld& world,
			[[maybe_unused]] const Entity& entity, [[maybe_unused]] const T& previewComponent) {}

		//--------- accessor -----------------------------------------------------

		// コミットを要求する
		void RequestCommit() { commitRequested_ = true; }

		// ドラフトのコンポーネントを返す
		T& GetDraft() { return draftComponent_; }
		const T& GetDraft() const { return draftComponent_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// インスペクターのヘッダのラベル
		std::string headerLabel_{};
		std::string componentTypeName_{};

		// 編集中のエンティティのUUID
		UUID editingEntityStableUUID_{};
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

		//--------- functions ----------------------------------------------------

		// 現在のドラフトをワールドから更新する
		void SyncDraftFromWorld(ECSWorld& world, const Entity& entity);
		// ドラフトの内容をワールドに反映する
		void CommitIfNeeded(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// プレビューが必要なら適用する
		void ApplyPreviewIfNeeded(ECSWorld& world, const Entity& entity);
	};

	//============================================================================
	//	SerializedComponentInspectorDrawer templateMethods
	//============================================================================

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::Draw(const EditorPanelContext& context,
		ECSWorld& world, const Entity& entity) {

		// 描画できない場合は何もしない
		if (!CanDraw(world, entity)) {
			return;
		}

		// エンティティが変わった、または編集中でない場合はワールドからドラフトを同期
		const UUID stableUUID = world.GetUUID(entity);
		if (editingEntityStableUUID_ != stableUUID || !isEditing_) {
			SyncDraftFromWorld(world, entity);
		}

		// ヘッダーが閉じている場合は描画しない
		if (!MyGUI::CollapsingHeader(headerLabel_.c_str())) {
			return;
		}

		// アイテムのアクティブ状態を追跡するフラグ
		bool anyItemActive = false;
		DrawFields(context, world, entity, anyItemActive);

		// 編集結果を適用
		ApplyPreviewIfNeeded(world, entity);

		// 編集中状態を更新
		isEditing_ = anyItemActive;

		// 必要ならコミット
		CommitIfNeeded(context, world, entity);
	}

	template<typename T>
	inline bool SerializedComponentInspectorDrawer<T>::CanDraw(ECSWorld& world, const Entity& entity) const {

		return world.HasComponent<T>(entity);
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::PushEditResult(const ValueEditResult& result, bool& anyItemActive) {

		InspectorDrawerCommon::AccumulateEditResult(result, anyItemActive, commitRequested_);

		if (result.valueChanged) {
			previewRequested_ = true;
		}
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::ApplyPreview(ECSWorld& world,
		const Entity& entity, const T& previewComponent) {

		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}
		world.GetComponent<T>(entity) = previewComponent;
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::SyncDraftFromWorld(ECSWorld& world, const Entity& entity) {

		// エンティティが存在しない、またはコンポーネントがない場合は何もしない
		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}

		// ワールドからドラフトを更新
		draftComponent_ = world.GetComponent<T>(entity);
		editingEntityStableUUID_ = world.GetUUID(entity);

		// ドラフトをワールドから同期したのでプレビュー状態をリセット
		previewActive_ = false;
		previewRequested_ = false;
		previewBeginComponent_ = T{};

		// ドラフトをワールドから同期した後の追加処理
		OnSyncDraftFromWorld(draftComponent_);
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::CommitIfNeeded(const EditorPanelContext& context,
		ECSWorld& world, const Entity& entity) {

		if (!commitRequested_) {
			return;
		}
		commitRequested_ = false;

		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}

		// コミット前の追加処理
		const T beforeComponent = previewActive_ ? previewBeginComponent_ : world.GetComponent<T>(entity);

		T afterComponent = draftComponent_;
		OnBeforeCommit(beforeComponent, afterComponent);

		nlohmann::json beforeData = beforeComponent;
		nlohmann::json afterData = afterComponent;

		// シリアライズ後のデータが同じならコミットしない
		if (beforeData == afterData) {
			previewActive_ = false;
			SyncDraftFromWorld(world, entity);
			return;
		}

		// コマンドを実行して変更をコミット
		context.host->ExecuteEditorCommand(std::make_unique<SetSerializedComponentCommand>(
			entity, componentTypeName_, beforeData, afterData));

		previewActive_ = false;
		SyncDraftFromWorld(world, entity);
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::ApplyPreviewIfNeeded(ECSWorld& world, const Entity& entity) {

		if (!previewRequested_) {
			return;
		}
		previewRequested_ = false;

		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}

		if (!previewActive_) {
			previewBeginComponent_ = world.GetComponent<T>(entity);
			previewActive_ = true;
		}

		T previewComponent = draftComponent_;
		OnBeforeCommit(previewBeginComponent_, previewComponent);

		// プレビューを適用
		ApplyPreview(world, entity, previewComponent);
		OnAfterPreviewApplied(world, entity, previewComponent);

		// プレビュー適用後のワールド状態へドラフトを再同期
		if (world.IsAlive(entity) && world.HasComponent<T>(entity)) {

			draftComponent_ = world.GetComponent<T>(entity);
			OnSyncDraftFromWorld(draftComponent_);
		}
	}

	template<typename T>
	template<typename DrawFunc>
	inline void SerializedComponentInspectorDrawer<T>::DrawField(bool& anyItemActive, DrawFunc&& drawFunc) {

		// ラムダで登録した関数フィールドを描画
		PushEditResult(std::forward<DrawFunc>(drawFunc)(), anyItemActive);
		ImGui::Separator();
	}
} // Engine