#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/IInspectorComponentDrawer.h>
#include "SerializedComponentEditSession.h"
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/Commands/Components/RemoveComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

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
		public IInspectorComponentDrawer,
		protected SerializedComponentEditHooks<T> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SerializedComponentInspectorDrawer(const std::string_view& headerLabel,
			const std::string_view& componentTypeName, bool drawHeader = true) :
			headerLabel_(headerLabel), drawHeader_(drawHeader), session_(componentTypeName) {}
		~SerializedComponentInspectorDrawer() = default;

		// インスペクター描画
		void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) override;

		//--------- accessor -----------------------------------------------------

		// 描画可能か
		bool CanDraw(ECSWorld& world, const Entity& entity) const override;
	protected:
		//============================================================================
		//	protected Methods
		//============================================================================
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
		virtual void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const T& component);
		// コミット前の追加処理
		virtual void OnBeforeCommit([[maybe_unused]] const T& beforeComponent, [[maybe_unused]] T& afterComponent) {}
		// ドラフトをコマンド保存用のjsonへ変換する
		virtual void SerializeDraft(ECSWorld& world, const Entity& entity,
			const T& component, nlohmann::json& out) const;

		// プレビューの適用
		virtual void ApplyPreview(ECSWorld& world, const Entity& entity, const T& previewComponent);
		virtual void OnAfterPreviewApplied([[maybe_unused]] ECSWorld& world,
			[[maybe_unused]] const Entity& entity, [[maybe_unused]] const T& previewComponent) {}

		//--------- accessor -----------------------------------------------------

		// コミットを要求する
		void RequestCommit() { session_.RequestCommit(); }

		// ドラフトのコンポーネントを返す
		T& GetDraft() { return session_.GetDraft(); }
		const T& GetDraft() const { return session_.GetDraft(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// インスペクターのヘッダのラベル
		std::string headerLabel_{};
		// falseなら派生Drawerが最上位ヘッダーを描画する
		bool drawHeader_ = true;

		// ドラフトとプレビューの編集セッション
		SerializedComponentEditSession<T> session_;

		//--------- functions ----------------------------------------------------

	};

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::PushEditResult(const ValueEditResult& result, bool& anyItemActive) {

		session_.PushEditResult(result, anyItemActive);
	}

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
		if (session_.NeedsSync(stableUUID)) {
			session_.SyncDraftFromWorld(world, entity, *this);
		}

		if (drawHeader_) {

			// ヘッダーを右クリックしたらコンポーネント削除メニューを出す
			const bool headerOpen = MyGUI::CollapsingHeader(headerLabel_.c_str());
			if (ImGui::BeginPopupContextItem()) {

				if (ImGui::MenuItem("Remove Component")) {
					if (context.host) {
						context.host->ExecuteEditorCommand(
							std::make_unique<RemoveComponentCommand>(entity, session_.GetComponentTypeName()));
					}
				}
				ImGui::EndPopup();
			}
			// ヘッダーが閉じている場合は中身を描画しない
			if (!headerOpen) {
				return;
			}
		}

		// アイテムのアクティブ状態を追跡するフラグ
		bool anyItemActive = false;
		DrawFields(context, world, entity, anyItemActive);

		// 編集結果を適用
		session_.ApplyPreviewIfNeeded(world, entity, *this);

		// 編集中状態を更新
		session_.SetEditing(anyItemActive);

		// 必要ならコミット
		session_.CommitIfNeeded(context, world, entity, *this);
	}

	template<typename T>
	inline bool SerializedComponentInspectorDrawer<T>::CanDraw(ECSWorld& world, const Entity& entity) const {

		return world.HasComponent<T>(entity);
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::ApplyPreview(ECSWorld& world,
		const Entity& entity, const T& previewComponent) {

		if (!world.IsAlive(entity) || !world.HasComponent<T>(entity)) {
			return;
		}
		world.GetComponent<T>(entity) = previewComponent;
		world.MarkComponentModified<T>(entity);
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::OnSyncDraftFromWorld(
		[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
		const T& component) {

		OnSyncDraftFromWorld(component);
	}

	template<typename T>
	inline void SerializedComponentInspectorDrawer<T>::SerializeDraft(
		[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
		const T& component, nlohmann::json& out) const {

		if constexpr (requires { SerializeComponentDraft(component, out); }) {
			SerializeComponentDraft(component, out);
		} else {
			out = component;
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
