#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/Utility/LogicPlacementSource.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseCache.h>

namespace Engine {

	//============================================================================
	//	LogicPlacementTool class
	//	形状の配置候補を確認して階層をまとめて生成する
	//============================================================================
	class LogicPlacementTool final : public IEditorTool {
	public:

		LogicPlacementTool();
		~LogicPlacementTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:

		ToolDescriptor descriptor_{
			.id = "engine.logic_placement", .name = "ロジック配置", .category = "配置",
			.owner = ToolOwner::Engine, .flags = ToolFlags::EditOnly, .order = 0,
		};
		LogicPlacementRequest request_;
		std::shared_ptr<const LogicPlacementResult> preview_;
		PrefabBaseCache prefabCache_;
		std::weak_ptr<const ECSWorldLifetime> observedWorld_;
		UUID observedScene_{};

		// プロジェクトごとのユーザー設定を読み込む
		void LoadSettings();
		// 配置元のシーン参照を含めず設定を保存する
		void SaveSettings() const;
	};
}
