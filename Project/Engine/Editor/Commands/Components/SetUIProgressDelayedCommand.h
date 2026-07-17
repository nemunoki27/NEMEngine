#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	SetUIProgressDelayedCommand class
	//	UIProgressの遅延表示用エンティティを切り替えるコマンド
	//============================================================================
	class SetUIProgressDelayedCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SetUIProgressDelayedCommand(const Entity& targetEntity, bool delayed);
		~SetUIProgressDelayedCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set UI Progress Delayed"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();
		UUID targetStableUUID_{};
		bool delayed_ = false;
		bool changedDelayedEntity_ = false;

		nlohmann::json beforeData_{};
		nlohmann::json afterData_{};
		EditorEntityTreeSnapshot delayedEntitySnapshot_{};

		//--------- functions ----------------------------------------------------

		// UIProgressのJSONを適用する
		bool ApplyComponent(EditorCommandContext& context, const nlohmann::json& data);
		// 遅延表示用エンティティを生成する
		bool EnableDelayed(EditorCommandContext& context, const Entity& target);
		// 遅延表示用エンティティを削除する
		bool DisableDelayed(EditorCommandContext& context, const Entity& target);
		// 遅延表示用エンティティを復元する
		bool RestoreDelayedEntity(EditorCommandContext& context);
		// 遅延表示用エンティティを破棄する
		void DestroyDelayedEntity(EditorCommandContext& context);
	};
} // Engine
