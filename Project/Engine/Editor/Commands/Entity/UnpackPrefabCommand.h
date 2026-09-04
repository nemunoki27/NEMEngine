#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>

// c++
#include <optional>
#include <vector>

namespace Engine {

	//============================================================================
	//	UnpackPrefabCommand class
	//	Prefabインスタンスのリンク解除をUndo対応で行うコマンド
	//============================================================================
	class UnpackPrefabCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		UnpackPrefabCommand(const Entity& root, PrefabUnpackMode mode);
		~UnpackPrefabCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;
		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Unpack Prefab"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct LinkChange {

			UUID entityStableUUID{};
			std::optional<PrefabLinkComponent> before{};
			std::optional<PrefabLinkComponent> after{};
		};

		//--------- variables ----------------------------------------------------

		Entity initialRoot_ = Entity::Null();
		UUID rootStableUUID_{};
		PrefabUnpackMode mode_ = PrefabUnpackMode::OutermostRoot;
		std::vector<LinkChange> changes_{};

		//--------- functions ----------------------------------------------------

		// 保存したPrefabリンク状態を適用する
		bool ApplyState(EditorCommandContext& context, bool useAfter) const;
	};
} // Engine
