#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string>
#include <string_view>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	RemoveComponentCommand class
	//	エンティティからコンポーネントを削除するコマンド
	//============================================================================
	class RemoveComponentCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RemoveComponentCommand(const Entity& targetEntity, const std::string_view& typeName);
		~RemoveComponentCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Remove Component"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};
		std::string typeName_{};

		// コンポーネントのデータを保存しておくためのjson
		nlohmann::json serializedComponent_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool ApplyRemove(EditorCommandContext& context);
		bool ApplyRestore(EditorCommandContext& context);
	};
} // Engine