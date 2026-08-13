#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <cstdint>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PerformanceCheckTool class
	//	モデルをグリッド配置して描画負荷を確認するツール
	//============================================================================
	class PerformanceCheckTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PerformanceCheckTool();
		~PerformanceCheckTool() override;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// パフォーマンスチェックウィンドウを描画
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// シーンごとに生成したグリッドと表示統計を保持する
		struct GridSceneState {

			UUID rootStableUUID{};
			Entity observedRoot = Entity::Null();
			uint64_t drawEntityCount = 0;
			uint64_t pointLightCount = 0;
			uint64_t totalVertexCount = 0;
		};

		//--------- variables ----------------------------------------------------

		static constexpr int32_t kMaxGridCount = 512;
		static constexpr uint64_t kMaxEntityCount = 1000000;

		ToolDescriptor descriptor_{
			.id = "engine.performance_check",
			.name = "パフォーマンスチェック",
			.category = "デバッグ",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 0,
		};

		bool openWindow_ = false;

		int32_t gridCountXZ_ = 10;
		int32_t gridCountY_ = 1;
		float gridWidth_ = 2.0f;
		AssetID model_{};

		bool placePointLights_ = false;
		bool pointLightShadows_ = false;
		int32_t pointLightCount_ = 200;
		float pointLightIntensity_ = 1.0f;
		float pointLightRadius_ = 8.0f;
		float pointLightDecay_ = 1.0f;

		std::unordered_map<UUID, GridSceneState> sceneStates_;
		std::unordered_map<AssetID, uint64_t> meshVertexCounts_;

		std::string statusMessage_;
		bool statusError_ = false;

		//--------- functions ----------------------------------------------------

		// パフォーマンスチェックウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// 生成ルートの変更時だけ描画統計を再集計する
		void RefreshStatistics(const EditorToolContext& context,
			GridSceneState& state, const Entity& root);
		// モデルの頂点数を既存のサブメッシュ解析経路から取得する
		uint64_t ResolveMeshVertexCount(AssetDatabase* assetDatabase, AssetID meshAssetID);
		// ユーザー設定からツール設定を読み込む
		void LoadSettings();
		// ユーザー設定へツール設定を保存する
		void SaveSettings() const;
	};
} // Engine
