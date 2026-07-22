#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ReparentEntityCommand class
	//	エンティティの親を変更するコマンド
	//============================================================================
	class ReparentEntityCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit ReparentEntityCommand(const Entity& targetEntity, UUID newParentStableUUID = UUID{});
		ReparentEntityCommand(const Entity& targetEntity, const Entity& newSkinnedEntity, std::string newJointName);
		~ReparentEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Reparent Entity"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 親子付け先と適用後のローカルTransform
		struct ParentState {

			// 通常親のエンティティUUID
			UUID parentStableUUID{};
			// ジョイントを持つスキンメッシュのシーンローカルID
			UUID skinnedLocalFileID{};
			// スキンメッシュが属するシーンインスタンスID
			UUID sceneInstanceID{};
			// 親にするジョイント名
			std::string jointName{};
			// 親子付け適用後のTransform
			TransformComponent transform{};
			// ジョイント親子付けか
			bool jointAttached = false;
			// Transformを保持しているか
			bool hasTransform = false;
		};

		//--------- variables ----------------------------------------------------

		// 初回実行時の対象エンティティ
		Entity initialTarget_ = Entity::Null();
		// 初回実行時のスキンメッシュエンティティ
		Entity initialSkinnedEntity_ = Entity::Null();

		// 対象エンティティUUID
		UUID targetStableUUID_{};
		// 変更前の親子付け状態
		ParentState oldState_{};
		// 変更後の親子付け状態
		ParentState newState_{};
		// 初回実行済みか
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 現在の親子付け状態を取得する
		bool CaptureState(ECSWorld& world, const Entity& entity, ParentState& state) const;
		// 指定した親子付け状態を適用する
		bool ApplyState(EditorCommandContext& context, const ParentState& state);
		// 親子付け先が同じか
		bool IsSameParent(const ParentState& lhs, const ParentState& rhs) const;
	};

	//============================================================================
	//	ReorderEntityCommand class
	//	同じ階層内でエンティティの表示順を変更するコマンド
	//============================================================================
	class ReorderEntityCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ReorderEntityCommand(const Entity& targetEntity, const Entity& anchorEntity, bool insertAfter);
		~ReorderEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Reorder Entity"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();
		Entity initialAnchor_ = Entity::Null();
		bool insertAfter_ = false;

		UUID targetStableUUID_{};
		UUID parentStableUUID_{};
		std::vector<UUID> oldOrder_{};
		std::vector<UUID> newOrder_{};

		//--------- functions ----------------------------------------------------

		// 指定した順序を適用する
		bool ApplyOrder(EditorCommandContext& context, const std::vector<UUID>& order);
	};
} // Engine

