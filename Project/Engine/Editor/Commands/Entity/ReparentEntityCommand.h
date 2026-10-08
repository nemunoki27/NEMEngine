#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	ReparentEntityCommand class
	//	Entityの親とJoint接続を変更する
	//============================================================================
	class ReparentEntityCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit ReparentEntityCommand(const Entity& targetEntity, UUID newParentStableUUID = UUID{});
		ReparentEntityCommand(const Entity& targetEntity, const Entity& newSkinnedEntity, std::string newJointName);
		~ReparentEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// 変更を取り消して再適用する
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
} // Engine
