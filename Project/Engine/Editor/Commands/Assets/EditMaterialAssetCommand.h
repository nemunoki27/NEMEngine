#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>

// c++
#include <filesystem>
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	EditMaterialAssetCommand class
	//	マテリアルの保存と履歴操作を同じ文書に適用する
	//============================================================================
	class EditMaterialAssetCommand final : public IEditorAssetCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		EditMaterialAssetCommand(AssetID asset, std::filesystem::path project,
			nlohmann::json before, nlohmann::json after);
		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		const char* GetName() const override { return "マテリアルを編集"; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		AssetID asset_{};
		std::filesystem::path project_;
		nlohmann::json before_;
		nlohmann::json after_;

		bool Apply(EditorCommandContext& context, const nlohmann::json& expected, const nlohmann::json& next) const;
	};
}
