#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PerformanceCheckTool class
	//	モデルをグリッド配置して描画負荷を確認するツール
	//============================================================================
	class PerformanceCheckTool : public IEditorTool {
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
			uint64_t renderRevision = 0;
			uint64_t assetRevision = 0;
		};

		// Meshの頂点数と解析した世代
		struct MeshVertexCount {

			uint64_t revision = 0; // Assetの更新世代
			uint64_t count = 0;	   // モデルの頂点数
		};

		//--------- variables ----------------------------------------------------

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
		std::unordered_map<AssetID, MeshVertexCount> meshVertexCounts_;
		// 統計が参照するWorldの寿命
		std::weak_ptr<const ECSWorldLifetime> observedWorld_;

		std::string statusMessage_;
		bool statusError_ = false;

		//--------- functions ----------------------------------------------------

		// パフォーマンスチェックウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// WorldとAssetの更新を描画統計へ反映する
		void RefreshStatistics(const EditorToolContext& context, GridSceneState& state, const Entity& root);
		// モデルの頂点数を既存のサブメッシュ解析経路から取得する
		uint64_t ResolveMeshVertexCount(AssetDatabase* assetDatabase, AssetID meshAssetID);
		// ユーザー設定からツール設定を読み込む
		void LoadSettings();
		// ユーザー設定へツール設定を保存する
		void SaveSettings() const;
	};
} // Engine
