#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	SetTransformCommand class
	//	トランスフォームのセットコマンド
	//============================================================================
	class SetTransformCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SetTransformCommand(const Entity& targetEntity, const TransformComponent& beforeTransform,
			const TransformComponent& afterTransform);
		~SetTransformCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// UndoとRedoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		// トランスフォームの近似比較
		static bool NearlyEqualTransform(const Engine::TransformComponent& lhs,
			const Engine::TransformComponent& rhs);

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Transform"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初回実行時の対象
		Entity initialTarget_ = Entity::Null();

		// UndoとRedoで解決する対象UUID
		UUID targetStableUUID_{};

		// 変更前のTransform
		TransformComponent beforeTransform_{};
		// 変更後のTransform
		TransformComponent afterTransform_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool ApplyTransform(EditorCommandContext& context, const TransformComponent& transform);
	};
} // Engine
