#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	SetTransformCommand class
	//	トランスフォームのセットコマンド
	//============================================================================
	class SetTransformCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SetTransformCommand(const Entity& targetEntity,
			const TransformComponent& beforeTransform,
			const TransformComponent& afterTransform);
		~SetTransformCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		// トランスフォームの近似比較
		static bool NearlyEqualTransform(const Engine::TransformComponent& lhs,
			const Engine::TransformComponent& rhs);

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Transform"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};

		TransformComponent beforeTransform_{};
		TransformComponent afterTransform_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool ApplyTransform(EditorCommandContext& context, const TransformComponent& transform);
	};
} // Engine