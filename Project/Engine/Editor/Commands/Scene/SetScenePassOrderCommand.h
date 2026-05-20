#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	SetScenePassOrderCommand class
	//	SceneHeader.passOrderを差し替えるEditor Command
	//============================================================================

	class SetScenePassOrderCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// コンストラクタ
		SetScenePassOrderCommand(std::vector<ScenePassDesc> beforePassOrder,
			std::vector<ScenePassDesc> afterPassOrder);
		// デストラクタ
		~SetScenePassOrderCommand() override = default;

		// passOrderの変更を実行する
		bool Execute(EditorCommandContext& context) override;
		// 変更前のpassOrderへ戻す
		void Undo(EditorCommandContext& context) override;
		// コマンド名を取得する
		const char* GetName() const override { return "SetScenePassOrder"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Undo用の変更前passOrder
		std::vector<ScenePassDesc> beforePassOrder_;
		// Redo / Execute用の変更後passOrder
		std::vector<ScenePassDesc> afterPassOrder_;

		//--------- functions ----------------------------------------------------

		// 指定したpassOrderを現在のSceneHeaderへ適用する
		bool Apply(EditorCommandContext& context, const std::vector<ScenePassDesc>& passOrder) const;
	};
} // Engine
