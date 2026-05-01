#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	InstantiatePrefabCommand class
	//	Prefabアセットからシーン上にインスタンスを生成するコマンド
	//============================================================================
	class InstantiatePrefabCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit InstantiatePrefabCommand(AssetID prefabAsset, UUID parentStableUUID = UUID{});
		~InstantiatePrefabCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Instantiate Prefab"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 生成元のPrefabアセット
		AssetID prefabAsset_{};
		// 生成先の親Entity。空の場合はルートに生成する
		UUID parentStableUUID_{};
		// 最後に生成したPrefabインスタンスのルートEntity
		UUID instantiatedRootStableUUID_{};

		//--------- functions ----------------------------------------------------

		// Prefab生成処理
		bool InstantiateInternal(EditorCommandContext& context);
	};
} // Engine
