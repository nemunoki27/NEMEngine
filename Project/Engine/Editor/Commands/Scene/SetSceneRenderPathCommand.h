#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	SetSceneRenderPathCommand class
	//	SceneHeaderの描画情報を差し替えるEditor Command
	//============================================================================

	class SetSceneRenderPathCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// コンストラクタ
		SetSceneRenderPathCommand(std::vector<SceneRenderTargetDesc> beforeRenderTargets,
			std::vector<ScenePassDesc> beforePassOrder,
			std::vector<SceneRenderTargetDesc> afterRenderTargets,
			std::vector<ScenePassDesc> afterPassOrder);
		// デストラクタ
		~SetSceneRenderPathCommand() override = default;

		// 描画情報の変更を実行する
		bool Execute(EditorCommandContext& context) override;
		// 変更前の描画情報へ戻す
		void Undo(EditorCommandContext& context) override;
		// コマンド名を取得する
		const char* GetName() const override { return "SetSceneRenderPath"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Undo用の変更前renderTargets
		std::vector<SceneRenderTargetDesc> beforeRenderTargets_;
		// Undo用の変更前passOrder
		std::vector<ScenePassDesc> beforePassOrder_;
		// Redo / Execute用の変更後renderTargets
		std::vector<SceneRenderTargetDesc> afterRenderTargets_;
		// Redo / Execute用の変更後passOrder
		std::vector<ScenePassDesc> afterPassOrder_;

		//--------- functions ----------------------------------------------------

		// 指定した描画情報を現在のSceneHeaderへ適用する
		bool Apply(EditorCommandContext& context,
			const std::vector<SceneRenderTargetDesc>& renderTargets,
			const std::vector<ScenePassDesc>& passOrder) const;
	};
} // Engine
