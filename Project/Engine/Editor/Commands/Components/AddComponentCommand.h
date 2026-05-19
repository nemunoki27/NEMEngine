#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <string_view>

namespace Engine {

	//============================================================================
	//	AddComponentCommand class
	//	コンポーネントをエンティティに追加するコマンド
	//============================================================================
	class AddComponentCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AddComponentCommand(const Entity& targetEntity, const std::string_view& typeName);
		~AddComponentCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Add Component"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};
		std::string typeName_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool ApplyAdd(EditorCommandContext& context);
		bool ApplyRemove(EditorCommandContext& context);
	};
} // Engine