#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Commands/Core/EditorSelectionSnapshot.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Utility/LogicPlacementSource.h>
#include <Engine/Core/Foundation/Geometry/LogicPlacement.h>

namespace Engine {

	//============================================================================
	//	PlaceObjectsCommand class
	//	親と配置した全階層を一つの履歴で扱う
	//============================================================================
	class PlaceObjectsCommand final : public IEditorCommand {
	public:

		explicit PlaceObjectsCommand(LogicPlacementRequest request);
		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;
		const char* GetName() const override { return "ロジック配置"; }
	private:

		LogicPlacementRequest request_;
		UUID rootID_{};
		EditorSelectionSnapshot previousSelection_;
		std::unique_ptr<DeleteEntityCommand> removal_;
		std::weak_ptr<const ECSWorldLifetime> lifetime_;

		// 配置方向とモデルの前方軸から姿勢を決める
		Quaternion ResolveRotation(const LogicPlacementPoint& point, Vector3 normal, Quaternion original) const;
	};
}
