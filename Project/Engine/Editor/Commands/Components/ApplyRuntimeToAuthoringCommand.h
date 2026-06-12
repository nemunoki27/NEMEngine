#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <string_view>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	ApplyRuntimeToAuthoringCommand class
	//	Play中のruntime値をEditWorldのauthoring componentへ適用する専用コマンド
	//============================================================================
	class ApplyRuntimeToAuthoringCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ApplyRuntimeToAuthoringCommand(const UUID& entityStableUUID, const std::string_view& typeName,
			const nlohmann::json& beforeData, const nlohmann::json& afterData);
		~ApplyRuntimeToAuthoringCommand() = default;

		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		const char* GetName() const override { return "Apply Runtime Values"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		bool Apply(EditorCommandContext& context, const nlohmann::json& data);

		//--------- variables ----------------------------------------------------

		UUID targetStableUUID_{};
		std::string typeName_{};
		nlohmann::json beforeData_{};
		nlohmann::json afterData_{};
	};
} // Engine
