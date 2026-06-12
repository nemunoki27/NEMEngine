#include "RenderPipelineUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

namespace Engine {

	//============================================================================
	//	RenderPipelineUtility functions
	//============================================================================

	void PreDispatchVisibleMeshSkinning(GraphicsCore& graphicsCore,
		const SceneExecutionContext& context, const RenderSceneBatch& renderBatch,
		RenderBackendRegistry& backendRegistry, RenderAssetLibrary& assetLibrary,
		PipelineStateCache& pipelineCache, MaterialResolver& materialResolver,
		const RenderPassPhaseBuckets& passBuckets) {

		// メッシュ描画クラスを取得し、登録されていないかビューが無効なら何もしない
		auto* baseBackend = backendRegistry.Find(RenderBackendID::Mesh);
		auto* meshBackend = dynamic_cast<MeshRenderBackend*>(baseBackend);
		if (!meshBackend || !context.view || !context.sceneInstance) {
			return;
		}

		// スキニングバッチ処理用の描画コンテキストを構築しビューやアセットの依存関係をまとめる
		RenderDrawContext drawContext{};
		drawContext.graphicsCore = &graphicsCore;
		drawContext.view = context.view;
		drawContext.cullingView = context.cullingView;
		drawContext.systemContext = context.systemContext;
		drawContext.batch = &renderBatch;
		drawContext.bufferRegistry = &context.bufferRegistry;
		drawContext.assetDatabase = context.assetDatabase;
		drawContext.assetLibrary = &assetLibrary;
		drawContext.pipelineCache = &pipelineCache;
		drawContext.materialResolver = &materialResolver;
		drawContext.forceVertexMeshVariant = context.forceVertexMeshVariant;

		// GPUのランタイム機能を確認し、プレビュー用途ではレイジーな更新を避けるためRayQuery系を選ばない場合がある
		drawContext.runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
		if (context.disableInlineRayTracing) {
			drawContext.runtimeFeatures.useInlineRayTracing = false;
			drawContext.runtimeFeatures.useDispatchRays = false;
		}

		// 連続するメッシュアイテムをまとめてスキニングバッチに投げるラムダでドローコール間のオーバーヘッドを削減
		auto runDispatch = [&](const std::vector<const RenderItem*>& items) {

			size_t begin = 0;
			while (begin < items.size()) {

				const RenderItem* first = items[begin];
				if (!first || first->backendID != RenderBackendID::Mesh) {
					++begin;
					continue;
				}

				// 同一のメッシュバックエンドで一括処理可能な範囲を探し、マテリアルや頂点バッファの構成が同一なら統合
				size_t end = begin + 1;
				while (end < items.size()) {
					const RenderItem* next = items[end];
					if (!next) {
						break;
					}
					if (next->backendID != first->backendID ||
						!meshBackend->CanBatch(*first, *next, drawContext.runtimeFeatures)) {
						break;
					}
					++end;
				}

				// バッチ実行でコンピュートシェーダにより頂点変形を並列処理
				meshBackend->PreDispatchSkinningBatch(drawContext, std::span(items.data() + begin, end - begin));
				begin = end;
			}
		};

		// 描画フェーズごとのバケットに対して処理を実行し不透明や透明などの順序を守って更新
		for (const RenderPassItemList& list : passBuckets.buckets) {
			if (!list.IsEmpty()) {
				runDispatch(list.items);
			}
		}
	}

	void CollectVisibleMeshAssetsForView(const RenderSceneBatch& renderBatch,
		UUID sceneInstanceID, const ResolvedRenderView& view,
		std::unordered_set<AssetID>& outMeshAssets) {

		// ビューが無効なら収集不可
		if (!view.valid) {
			return;
		}

		// 全描画アイテムから指定ビューのマスクに合致するメッシュアセットを抽出しTLAS構築や事前ロードに利用
		for (const RenderItem& item : renderBatch.GetItems()) {

			// 指定があればシーンインスタンスでフィルタしサブシーン描画用に絞る
			if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
				continue;
			}
			if (item.backendID != RenderBackendID::Mesh) {
				continue;
			}

			// カメラの可視マスク判定でMain/UI等のドメインが一致しレイヤーが許可されているか確認
			const ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
			if (!camera || (item.visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			// 有効なメッシュアセットがあればリストに加える
			const MeshRenderPayload* payload = renderBatch.GetPayload<MeshRenderPayload>(item);
			if (payload && payload->mesh) {
				outMeshAssets.insert(payload->mesh);
			}
		}
	}

	bool IsEntityInPreviewTree(ECSWorld& world, Entity root, Entity entity) {

		// どちらかが無効なら中断
		if (!world.IsAlive(root) || !world.IsAlive(entity)) {
			return false;
		}
		// ルート自身なら当然対象
		if (root == entity) {
			return true;
		}

		// 親を辿ってルートに到達するか判定し指定されたルート以下の階層のみをプレビュー対象にする
		Entity current = entity;
		while (const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(current)) {

			if (!world.IsAlive(hierarchy->parent)) {
				break;
			}
			if (hierarchy->parent == root) {
				return true;
			}
			current = hierarchy->parent;
		}
		return false;
	}

	UUID ResolveEntitySceneInstanceID(ECSWorld& world, Entity entity, UUID fallback) {

		// Entityから所属シーンを取得し、なければ通常はアクティブシーンのフォールバックを返す
		UUID id = SceneObjectUtility::GetSceneInstanceID(world, entity);
		return id ? id : fallback;
	}

	void BuildPreviewPassBuckets(ECSWorld& world, Entity root,
		const RenderSceneBatch& renderBatch, const ResolvedRenderView& view,
		RenderPassPhaseBuckets& outBuckets, std::vector<AssetID>& outMeshAssets) {

		// 出力バッファをクリア
		outBuckets.Clear();
		outMeshAssets.clear();

		// 重複を防ぐためのセット
		std::unordered_set<uint64_t> meshAssetSet{};

		// 全描画アイテムを走査して、プレビュー対象のEntityツリーに属するものだけを収集
		// ツールウィンドウなどで特定のオブジェクトだけを独立して描画するために使用
		for (const RenderItem& item : renderBatch.GetItems()) {

			// プレビュー対象のツリー外ならスキップ
			if (!IsEntityInPreviewTree(world, root, item.entity)) {
				continue;
			}

			// 指定カメラからの可視判定でメインビューと同じカリングロジックを適用
			const ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
			if (!camera || (item.visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			// フェーズごとにOpaque/Transparent等でバケット分けし後の描画ループで使用
			outBuckets.Get(item.renderPhase).items.emplace_back(&item);

			// メッシュアセットの依存関係を収集しレイトレTLAS構築やマテリアル解決に使用
			if (item.backendID == RenderBackendID::Mesh) {
				if (const auto* payload = renderBatch.GetPayload<MeshRenderPayload>(item)) {
					if (payload->mesh) {
						meshAssetSet.emplace(payload->mesh.value);
					}
				}
			}
		}

		// セットからリストへ変換してアセット読み込み要求に備える
		outMeshAssets.reserve(meshAssetSet.size());
		for (uint64_t meshValue : meshAssetSet) {
			outMeshAssets.emplace_back(AssetID{ meshValue });
		}
	}

} // Engine
