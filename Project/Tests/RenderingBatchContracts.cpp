#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>
#include <Engine/Core/Rendering/Core/GraphicsFeatureSelection.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchViewResources.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderExtractorRegistry.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Registry/LightExtractorRegistry.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneGeometryUtility.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshletBuilder.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <unordered_map>

namespace NEMTests {

	bool TestMeshBatchInvalidation() {

		using namespace Engine;
		// 検索キーで別WorldとEntityの世代を区別
		ECSWorld keyWorld(ECSWorldKind::Runtime);
		ECSWorld otherKeyWorld(ECSWorldKind::Runtime);
		const WorldEntityKey key{ &keyWorld, Entity{ 1, 2 } };
		const WorldEntityKey otherWorldKey{ &otherKeyWorld, key.entity };
		const WorldEntityKey otherIndex{ &keyWorld, Entity{ 2, 2 } };
		const WorldEntityKey otherGeneration{ &keyWorld, Entity{ 1, 3 } };
		std::unordered_map<WorldEntityKey, int, WorldEntityKeyHash> keys;
		keys[key] = 1;
		keys[otherWorldKey] = 2;
		keys[otherIndex] = 3;
		keys[otherGeneration] = 4;
		if (keys.size() != 4 || keys.at(WorldEntityKey{ &keyWorld, Entity{ 1, 2 } }) != 1 ||
			keys.at(otherWorldKey) != 2 || keys.at(otherIndex) != 3 || keys.at(otherGeneration) != 4) return false;

		// 同じポーズでもPSO更新と計算失敗で結果を失効させる
		MeshBatchResources skinning;
		if (skinning.CanReuseSkinningOutput(1) || skinning.IsSkinningDispatched()) return false;
		skinning.MarkSkinningDispatched(1);
		if (!skinning.CanReuseSkinningOutput(1) || skinning.CanReuseSkinningOutput(2) ||
			skinning.CanReuseSkinningOutput(0) || skinning.GetSkinningResultGeneration() != 1) return false;
		skinning.SetSkinningAvailable(false);
		if (skinning.CanReuseSkinningOutput(1) || skinning.IsSkinningDispatched()) return false;
		skinning.MarkSkinningDispatched(2);
		if (!skinning.CanReuseSkinningOutput(2) || skinning.GetSkinningResultGeneration() != 2) return false;
		skinning.Finalize();
		if (skinning.CanReuseSkinningOutput(2) || skinning.IsSkinningDispatched()) return false;
		// Worldを変更せず登録内容だけを入れ替える
		struct CountingRenderExtractor final : IRenderItemExtractor {
			int* count;
			explicit CountingRenderExtractor(int& value) : count(&value) {}
			void Extract(ECSWorld&, RenderSceneBatch&) override { ++*count; }
		};
		struct CountingLightExtractor final : ILightExtractor {
			int* count;
			explicit CountingLightExtractor(int& value) : count(&value) {}
			void Extract(ECSWorld&, FrameLightBatch&) override { ++*count; }
		};
		ECSWorld registryWorld(ECSWorldKind::Runtime);
		RenderSceneBatch registryBatch;
		FrameLightBatch registryLights;
		RenderExtractorRegistry renderExtractors;
		LightExtractorRegistry lightExtractors;
		int renderCount = 0;
		int lightCount = 0;
		for (int generation = 1; generation <= 2; ++generation) {
			renderExtractors.Register(std::make_unique<CountingRenderExtractor>(renderCount));
			lightExtractors.Register(std::make_unique<CountingLightExtractor>(lightCount));
			for (int repeat = 0; repeat < 2; ++repeat) {
				renderExtractors.BuildBatch(registryWorld, registryBatch);
				lightExtractors.BuildBatch(registryWorld, registryLights);
			}
			if (renderCount != generation || lightCount != generation) return false;
			renderExtractors.Clear();
			lightExtractors.Clear();
		}
		// 別Registryの同じ件数を旧登録とみなさない
		RenderExtractorRegistry otherRenderExtractors;
		LightExtractorRegistry otherLightExtractors;
		otherRenderExtractors.Register(std::make_unique<CountingRenderExtractor>(renderCount));
		otherLightExtractors.Register(std::make_unique<CountingLightExtractor>(lightCount));
		otherRenderExtractors.BuildBatch(registryWorld, registryBatch);
		otherLightExtractors.BuildBatch(registryWorld, registryLights);
		if (renderCount != 3 || lightCount != 3) return false;
		// 再抽出と同frameの追加移動でも前frameの行列を維持する
		struct MotionExtractor final : IRenderItemExtractor {
			Entity entity;
			explicit MotionExtractor(Entity value) : entity(value) {}
			void Extract(ECSWorld& world, RenderSceneBatch& batch) override {
				RenderItem item;
				item.world = &world;
				item.entity = entity;
				item.worldMatrix = world.GetComponent<TransformComponent>(entity).worldMatrix;
				batch.Add(std::move(item));
			}
		};
		const Entity moving = SceneAuthoring::CreateGameObject(registryWorld, "Motion");
		RenderExtractorRegistry motionExtractor;
		motionExtractor.Register(std::make_unique<MotionExtractor>(moving));
		RenderSceneBatch motionBatch;
		GraphicsFrameState::BeginFrame(0);
		motionExtractor.BuildBatch(registryWorld, motionBatch);
		GraphicsFrameState::BeginFrame(0);
		for (float x : { 4.0f, 6.0f }) {
			registryWorld.GetComponent<TransformComponent>(moving).worldMatrix.m[3][0] = x;
			registryWorld.MarkRenderDataModified();
			motionExtractor.BuildBatch(registryWorld, motionBatch);
			const auto& item = motionBatch.GetItems().front();
			if (item.worldMatrix.m[3][0] != x || item.previousWorldMatrix.m[3][0] != 0.0f ||
				item.motionFrameSerial != GraphicsFrameState::GetFrameSerial()) {
				std::cerr << "Motion re-extraction: current=" << item.worldMatrix.m[3][0] << " previous=" <<
					item.previousWorldMatrix.m[3][0] << " serial=" << item.motionFrameSerial << " frame=" << GraphicsFrameState::GetFrameSerial() << '\n';
				return false;
			}
		}
		registryWorld.GetComponent<TransformComponent>(moving).worldMatrix.m[3][0] = 8.0f;
		const std::array<Entity, 1> movingEntities{ moving };
		motionBatch.RefreshTransforms(registryWorld, movingEntities);
		if (motionBatch.GetItems().front().previousWorldMatrix.m[3][0] != 0.0f) {
			std::cerr << "Motion changed twice in one frame\n";
			return false;
		}
		GraphicsFrameState::BeginFrame(0);
		registryWorld.GetComponent<TransformComponent>(moving).worldMatrix.m[3][0] = 10.0f;
		motionBatch.RefreshAllTransforms();
		if (motionBatch.GetItems().front().previousWorldMatrix.m[3][0] != 8.0f) {
			std::cerr << "Motion previous frame=" << motionBatch.GetItems().front().previousWorldMatrix.m[3][0] << '\n';
			return false;
		}
		// Canvasの描画行列変更を抽出へ伝え、通常Transformで上書きしない
		UIRuntimeService uiRuntime;
		registryWorld.AddComponent<CanvasComponent>(moving);
		uiRuntime.Build(registryWorld, Vector2(640.0f, 360.0f));
		const uint64_t uiRevision = registryWorld.GetRenderDataRevision();
		uiRuntime.Build(registryWorld, Vector2(640.0f, 360.0f));
		if (registryWorld.GetRenderDataRevision() != uiRevision) return false;
		registryWorld.GetComponent<CanvasComponent>(moving).scaleFactor = 2.0f;
		uiRuntime.Build(registryWorld, Vector2(640.0f, 360.0f));
		if (registryWorld.GetRenderDataRevision() == uiRevision) return false;
		const auto* uiElement = uiRuntime.Find(registryWorld, moving);
		if (!uiElement) return false;
		const Matrix4x4 screenMatrix = uiElement->screenMatrix;
		RenderSceneBatch uiBatch;
		RenderItem uiItem;
		uiItem.world = &registryWorld;
		uiItem.entity = moving;
		uiItem.cameraDomain = RenderCameraDomain::Screen;
		uiItem.worldMatrix = screenMatrix;
		uiBatch.Add(std::move(uiItem));
		uiBatch.SetSource(&registryWorld, registryWorld.GetRenderDataRevision(), registryWorld.GetRenderTransformRevision());
		uiBatch.RefreshTransforms(registryWorld, movingEntities);
		uiBatch.RefreshAllTransforms();
		if (uiBatch.GetItems().front().worldMatrix != screenMatrix) return false;
		// Worldに変更がなくても即時Lineの追加と破棄を反映する
		auto& immediateLines = LineImmediateBuffer::GetInstance();
		immediateLines.BeginFrame();
		RenderExtractorRegistry lineExtractors;
		lineExtractors.Register(std::make_unique<LineRenderItemExtractor>());
		RenderSceneBatch lineBatch;
		lineExtractors.BuildBatch(registryWorld, lineBatch);
		if (!lineBatch.GetItems().empty()) return false;
		std::array<LinePoint, 2> linePoints{};
		linePoints[1].position = Vector3(1.0f, 0.0f, 0.0f);
		immediateLines.AddPolyline(linePoints.data(), 2, true, false, false, {});
		lineExtractors.BuildBatch(registryWorld, lineBatch);
		if (lineBatch.GetItems().size() != 1) return false;
		immediateLines.BeginFrame();
		lineExtractors.BuildBatch(registryWorld, lineBatch);
		if (!lineBatch.GetItems().empty()) return false;
		// Lineが別Entityへ追従する行列もTransform更新後に再抽出する
		const Entity line = SceneAuthoring::CreateGameObject(registryWorld, "FollowingLine");
		registryWorld.AddComponent<LineRendererComponent>(line);
		SetLinePoints(registryWorld, line, linePoints);
		auto& lineRenderer = registryWorld.GetComponent<LineRendererComponent>(line);
		lineRenderer.useWorldSpace = false;
		lineRenderer.parentLocalFileID = registryWorld.GetComponent<SceneObjectComponent>(moving).localFileID;
		lineExtractors.BuildBatch(registryWorld, lineBatch);
		if (lineBatch.GetItems().size() != 1 || lineBatch.CanRefreshTransforms()) return false;
		registryWorld.GetComponent<TransformComponent>(moving).worldMatrix.m[3][0] = 20.0f;
		registryWorld.MarkTransformConsumersModified(ComponentChangeChannel::Render, movingEntities);
		lineExtractors.BuildBatch(registryWorld, lineBatch);
		if (lineBatch.GetItems().front().worldMatrix.m[3][0] != 20.0f) return false;
		// UIのcacheもWorldの同一アドレス再利用を区別する
		std::optional<ECSWorld> uiWorld;
		uiWorld.emplace(ECSWorldKind::Runtime);
		const Entity oldCanvas = SceneAuthoring::CreateGameObject(*uiWorld, "OldCanvas");
		uiWorld->AddComponent<CanvasComponent>(oldCanvas);
		uiRuntime.Build(*uiWorld, Vector2(640.0f, 360.0f));
		uiWorld.reset();
		uiWorld.emplace(ECSWorldKind::Runtime);
		if (uiRuntime.Find(*uiWorld, oldCanvas) || !uiRuntime.GetElements(*uiWorld).empty()) return false;
		// 同じアドレスへ作り直したWorldの旧cacheを拒否する
		std::optional<ECSWorld> reusedWorld;
		reusedWorld.emplace(ECSWorldKind::Runtime);
		RenderSceneBatch reusedBatch;
		FrameLightBatch reusedLights;
		auto oldLifetime = reusedWorld->GetLifetime();
		reusedBatch.SetSource(&*reusedWorld, reusedWorld->GetRenderDataRevision(), reusedWorld->GetRenderTransformRevision());
		reusedLights.SetSource(&*reusedWorld, reusedWorld->GetRenderDataRevision());
		if (!reusedLights.MatchesSource(&*reusedWorld, reusedWorld->GetRenderDataRevision())) return false;
		if (!reusedBatch.MatchesStructure(&*reusedWorld, reusedWorld->GetRenderDataRevision())) return false;
		reusedWorld.reset();
		reusedWorld.emplace(ECSWorldKind::Runtime);
		if (oldLifetime->IsAlive() || reusedBatch.MatchesStructure(&*reusedWorld, reusedWorld->GetRenderDataRevision())) return false;
		if (reusedLights.MatchesSource(&*reusedWorld, reusedWorld->GetRenderDataRevision())) return false;
		// Mesh側のcacheもWorldの個体を区別する
		MeshGPUResource reusedMesh;
		MeshBatchIdentityCache reusedCache;
		RenderItem reusedItem;
		reusedItem.world = &*reusedWorld;
		reusedItem.entity = SceneAuthoring::CreateGameObject(*reusedWorld, "Reused");
		reusedItem.payload = reusedBatch.PushPayload(MeshRenderPayload{});
		const std::array<const RenderItem*, 1> reusedItems{ &reusedItem };
		reusedCache.Capture(reusedBatch, reusedItems, reusedMesh);
		reusedWorld.reset();
		reusedWorld.emplace(ECSWorldKind::Runtime);
		reusedItem.entity = SceneAuthoring::CreateGameObject(*reusedWorld, "Reused");
		if (reusedCache.Matches(reusedBatch, reusedItems, reusedMesh)) return false;
		ECSWorld world(ECSWorldKind::Runtime);
		const Entity rain = SceneAuthoring::CreateGameObject(world, "Rain");
		const Entity stage = SceneAuthoring::CreateGameObject(world, "Stage");
		const Entity middle = SceneAuthoring::CreateGameObject(world, "Middle");
		const Entity other = SceneAuthoring::CreateGameObject(world, "Other");
		RenderSceneBatch batch;
		MeshRenderPayload payload{};
		RenderItem a{}, b{}, c{}, d{};
		a.world = b.world = c.world = d.world = &world;
		a.entity = rain; b.entity = middle; c.entity = stage; d.entity = other;
		a.payload = b.payload = c.payload = d.payload = batch.PushPayload(payload);
		std::array<const RenderItem*, 3> items{ &a, &b, &c };
		std::array<const RenderItem*, 1> stageItems{ &c };
		MeshGPUResource mesh;
		MeshBatchIdentityCache rainCache, stageCache;
		rainCache.Capture(batch, items, mesh);
		stageCache.Capture(batch, stageItems, mesh);
		const uint64_t renderRevision = world.GetRenderDataRevision();
		world.MarkMeshColorModified(rain);
		const uint64_t colorRevision = world.GetMeshColorRevision(rain);
		world.MarkMeshColorModified(rain);
		if (world.GetRenderDataRevision() != renderRevision || world.GetMeshColorRevision(rain) <= colorRevision ||
			world.GetMeshColorRevision(stage) != 0 || !rainCache.Matches(batch, items, mesh) ||
			!stageCache.Matches(batch, stageItems, mesh)) {
			return false;
		}
		// 先頭と末尾が同じでも中央のEntityやサブメッシュが異なれば再構築する
		items[1] = &d;
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		items[1] = &b;
		payload.subMeshIndex = 1;
		b.payload = batch.PushPayload(payload);
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		b.payload = a.payload;
		// 描画設定、順序、Worldの違いも同じバッチとして扱わない
		b.material = AssetGUID::New();
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		b.material = {};
		b.receiveShadows = false;
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		b.receiveShadows = true;
		b.surfaceMode = MaterialSurfaceMode::Transparent;
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		b.surfaceMode = MaterialSurfaceMode::Opaque;
		std::swap(items[0], items[1]);
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		std::swap(items[0], items[1]);
		ECSWorld otherWorld(ECSWorldKind::Runtime);
		b.world = &otherWorld;
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		b.world = &world;
		if (!rainCache.Matches(batch, items, mesh)) { return false; }
		world.MarkRenderDataModified(rain);
		if (rainCache.Matches(batch, items, mesh) || !stageCache.Matches(batch, stageItems, mesh)) {
			return false;
		}
		// 全体通知とMeshの再読み込みは確実にキャッシュを無効化する
		rainCache.Capture(batch, items, mesh);
		world.MarkRenderDataModified();
		if (rainCache.Matches(batch, items, mesh) || stageCache.Matches(batch, stageItems, mesh)) {
			return false;
		}
		rainCache.Capture(batch, items, mesh);
		++mesh.reloadGeneration;
		if (rainCache.Matches(batch, items, mesh)) { return false; }
		--mesh.reloadGeneration;
		// 色だけの更新もRaytracingが参照する内容世代へ伝える
		batch.SetMaterialSource(world.GetMeshColorRevision());
		const uint64_t contents = batch.GetSourceRevision();
		world.MarkMeshColorModified(rain);
		batch.SetMaterialSource(world.GetMeshColorRevision());
		if (batch.GetSourceRevision() == contents) { return false; }
		const uint64_t unchanged = batch.GetSourceRevision();
		batch.SetMaterialSource(world.GetMeshColorRevision());
		if (batch.GetSourceRevision() != unchanged) { return false; }
		// 解除後は古い構成を使わず再登録する
		rainCache.Clear();
		if (rainCache.Matches(batch, items, mesh)) return false;
		rainCache.Capture(batch, items, mesh);
		if (!rainCache.Matches(batch, items, mesh)) return false;
		world.DestroyEntity(rain);
		world.FlushPendingDestroyEntities();
		return world.GetMeshColorRevision(rain) == 0 && world.GetEntityRenderRevision(rain) == 0;
	}
}
