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
	//	SetSerializedComponentCommand class
	//	コンポーネントをシリアライズされたデータで上書きするコマンド
	//============================================================================
	class SetSerializedComponentCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SetSerializedComponentCommand(const Entity& targetEntity, const std::string_view& typeName,
			const nlohmann::json& beforeData, const nlohmann::json& afterData);
		~SetSerializedComponentCommand() = default;

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
		nlohmann::json beforeData_{};
		nlohmann::json afterData_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool Apply(EditorCommandContext& context, const nlohmann::json& data);
	};
} // Engine
