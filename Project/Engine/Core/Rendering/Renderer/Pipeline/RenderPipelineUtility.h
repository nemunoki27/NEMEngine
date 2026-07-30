#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	RenderPipelineUtility functions
	//	レンダリングパイプラインで共通して使用するユーティリティ関数群
	//============================================================================

	// RenderView単位の可視性判定後に必要な分だけバッチ処理してスキニングを実行する
	void PreDispatchVisibleMeshSkinning(GraphicsCore& graphicsCore,
		const SceneExecutionContext& context, const RenderSceneBatch& renderBatch,
		RenderBackendRegistry& backendRegistry, RenderAssetLibrary& assetLibrary,
		PipelineStateCache& pipelineCache, MaterialResolver& materialResolver,
		const RenderPassPhaseBuckets& passBuckets);

	// レイトレーシングシーンに含まれる全スキンメッシュをビュー可視性に依存せず更新する
	void PreDispatchSceneMeshSkinning(GraphicsCore& graphicsCore,
		const SceneExecutionContext& context, const RenderSceneBatch& renderBatch,
		RenderBackendRegistry& backendRegistry, RenderAssetLibrary& assetLibrary,
		PipelineStateCache& pipelineCache, MaterialResolver& materialResolver);

	// ビューに対して可視なメッシュアセットIDを収集する
	void CollectVisibleMeshAssetsForView(const RenderSceneBatch& renderBatch,
		UUID sceneInstanceID, const ResolvedRenderView& view,
		std::unordered_set<AssetID>& outMeshAssets);

	// 指定Entityがrootの子階層であるプレビュー対象の木構造に含まれているか確認する
	bool IsEntityInPreviewTree(ECSWorld& world, Entity root, Entity entity);

	// Entityが所属するシーンインスタンスIDを取得する
	UUID ResolveEntitySceneInstanceID(ECSWorld& world, Entity entity, UUID fallback);

	// プレビュー対象の描画アイテムだけを描画フェーズごとに振り分ける
	void BuildPreviewPassBuckets(ECSWorld& world, Entity root,
		const RenderSceneBatch& renderBatch, const ResolvedRenderView& view,
		RenderPassPhaseBuckets& outBuckets, std::vector<AssetID>& outMeshAssets);

} // Engine
