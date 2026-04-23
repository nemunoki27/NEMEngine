#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string>
#include <string_view>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	AddScriptEntryCommand class
	//	ScriptComponent内のScriptEntryを追加するコマンド
	//============================================================================
	class AddScriptEntryCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AddScriptEntryCommand(const Entity& targetEntity, const std::string_view& typeName = {},
			AssetID scriptAsset = {});
		~AddScriptEntryCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Add Script"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();
		UUID targetStableUUID_{};

		std::string typeName_{};
		AssetID scriptAsset_{};
		nlohmann::json beforeData_{};
		bool createdComponent_ = false;

		//--------- functions ----------------------------------------------------

		// ScriptEntryを追加する
		bool ApplyAdd(EditorCommandContext& context);
		// 追加前の状態へ戻す
		bool ApplyRestore(EditorCommandContext& context);
	};
} // Engine
