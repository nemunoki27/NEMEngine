#include "LineRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

//============================================================================
//	LineRenderItemExtractor classMethods
//============================================================================
void Engine::LineRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<LineRendererComponent>([&](const Entity& entity, const LineRendererComponent& renderer) {

		const std::span<const LinePoint> points = GetLinePoints(world, entity);
		// 描画可能か
		if (!RenderItemExtract::IsVisible(world, entity, renderer.visible)) {
			return;
		}
		// 線分にならない点数は描かない
		if (points.size() < 2) {
			return;
		}

		// ペイロードは抽出フレーム中に有効なDynamicBufferの連続領域を参照する
		LineRenderPayload payload{};
		payload.points = points.data();
		payload.pointCount = static_cast<uint32_t>(points.size());
		payload.loop = renderer.loop;
		payload.is2D = renderer.is2D;
		payload.useWorldSpace = renderer.useWorldSpace;
		payload.materialOverrides = &renderer.parameterOverrides.Get();

		// 親追従の行列を決める、useWorldSpaceなら描画時に使われないので単位でよい
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		if (!renderer.useWorldSpace) {

			// 親localFileIDが未設定なら自身のTransformを親にする
			Entity parentEntity = entity;
			if (renderer.parentLocalFileID.value != 0) {

				const Entity found = SceneObjectUtility::FindByLocalFileID(world, renderer.parentLocalFileID);
				if (world.IsAlive(found)) {
					parentEntity = found;
				}
			}
			Matrix4x4 follow = BuildParentFollowMatrix(RenderItemExtract::GetWorldMatrix(world, parentEntity),
				renderer.ignoreParentScale, renderer.ignoreParentRotation);

			// 各セグメント中点を長さで重み付けした線全体の中心をピボットにする
			Vector3 centroid = Vector3::AnyInit(0.0f);
			float totalLength = 0.0f;
			for (size_t i = 0; i + 1 < points.size(); ++i) {

				const Vector3 mid = (points[i].position + points[i + 1].position) * 0.5f;
				const float segmentLength = (points[i + 1].position - points[i].position).Length();
				centroid += mid * segmentLength;
				totalLength += segmentLength;
			}
			// loopは終点と始点の区間も含める
			if (renderer.loop && points.size() >= 3) {

				const Vector3 mid = (points.back().position + points.front().position) * 0.5f;
				const float segmentLength = (points.front().position - points.back().position).Length();
				centroid += mid * segmentLength;
				totalLength += segmentLength;
			}
			centroid = totalLength > 1e-6f ? centroid / totalLength : points.front().position;

			// 平行移動だけ親に追従させ、回転拡縮はピボット中心で行う
			// M3x3*(p-C)+C+Tpになるよう平行移動成分を組み替える、TransferNormalは平行移動を含まないM3x3変換
			const Vector3 parentTranslation = follow.GetTranslationValue();
			const Vector3 rotatedScaledCentroid = Vector3::TransferNormal(centroid, follow);
			const Vector3 newTranslation = centroid + parentTranslation - rotatedScaledCentroid;
			follow.m[3][0] = newTranslation.x;
			follow.m[3][1] = newTranslation.y;
			follow.m[3][2] = newTranslation.z;
			worldMatrix = follow;
		}

		// 描画アイテムの構築
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, renderer, worldMatrix);
		item.backendID = RenderBackendID::Line;
		item.material = renderer.material;
		// 個別マテリアルパラメータを持つアイテムは専用cbufferが要るので、エンティティ単位で一意化して単独描画にする
		item.batchKey = renderer.parameterOverrides.empty() ? std::hash<AssetID>{}(renderer.material) :
			((static_cast<uint64_t>(entity.generation) << 32) | entity.index);
		item.cameraDomain = renderer.is2D ? RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective;
		item.payload = batch.PushPayload(payload);
		// 描画アイテムをバッチに追加
		batch.Add(std::move(item));
		});

	// 即時描画はEntityに紐づかないので、シーンフィルタを通すためアクティブシーンのinstanceIDを使う
	UUID activeSceneInstanceID{};
	if (const SceneInstance* activeScene = world.GetCommandServices().sceneInstances ?
		world.GetCommandServices().sceneInstances->GetActive() : nullptr) {
		activeSceneInstanceID = activeScene->instanceID;
	}

	// C#等から発行された即時ライン描画をバッチへ流す、点列はEntityに紐づかずワールド絶対座標
	const LineImmediateBuffer& immediate = LineImmediateBuffer::GetInstance();
	for (const LineImmediateBuffer::Entry& entry : immediate.GetEntries()) {

		if (entry.pointCount < 2) {
			continue;
		}

		LineRenderPayload payload{};
		payload.points = immediate.GetPoints(entry);
		payload.pointCount = entry.pointCount;
		payload.connected = entry.connected;
		payload.loop = entry.loop;
		payload.is2D = entry.is2D;
		payload.useWorldSpace = true;

		RenderItem item{};
		item.world = &world;
		item.sceneInstanceID = activeSceneInstanceID;
		item.backendID = RenderBackendID::Line;
		item.renderPhase = entry.queue;
		item.blendMode = entry.blendMode;
		item.material = entry.material;
		// 同一マテリアルの即時描画はまとめて1本の頂点バッファにする
		item.batchKey = std::hash<AssetID>{}(entry.material);
		item.cameraDomain = entry.is2D ? RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective;
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
	}
}
