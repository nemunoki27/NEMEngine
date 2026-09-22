#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>
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
#include <utility>

namespace NEMTests {

	bool TestBuiltinShaderSources() {

		constexpr std::array<const char*, 10> references = {
			Engine::BuiltinShaderSource::Skybox::VS,
			Engine::BuiltinShaderSource::Skybox::PS,
			Engine::BuiltinShaderSource::Line::GeometryVS,
			Engine::BuiltinShaderSource::Line::GeometryGS,
			Engine::BuiltinShaderSource::Line::GeometryPS,
			Engine::BuiltinShaderSource::Line::AnalyticGridVS,
			Engine::BuiltinShaderSource::Line::AnalyticGridPS,
			Engine::BuiltinShaderSource::Editor::PickMeshRasterPS,
			Engine::BuiltinShaderSource::Editor::SceneOverlaySpriteVS,
			Engine::BuiltinShaderSource::Editor::SceneOverlaySpritePS,
		};

		for (const char* reference : references) {

			if (!Engine::TryParseAssetGUID32Hex(reference)) {
				return false;
			}
			const std::filesystem::path path = Engine::ShaderSourcePath::Resolve(reference);
			std::error_code ec;
			if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec) {
				return false;
			}
		}
		return Engine::ShaderSourcePath::Resolve("b2995658d93cd4ab").empty();
	}

	bool TestMeshLODGeneration() {

		constexpr uint32_t gridSize = 32;
		Engine::ImportedMeshAsset mesh{};
		mesh.vertices.reserve(
			static_cast<size_t>(gridSize + 1) *
			static_cast<size_t>(gridSize + 1));
		for (uint32_t z = 0; z <= gridSize; ++z) {
			for (uint32_t x = 0; x <= gridSize; ++x) {

				const float u =
					static_cast<float>(x) /
					static_cast<float>(gridSize);
				const float v =
					static_cast<float>(z) /
					static_cast<float>(gridSize);
				Engine::MeshVertex vertex{};
				vertex.position =
					Engine::Vector4(u, 0.0f, v, 1.0f);
				vertex.normal =
					Engine::Vector3(0.0f, 1.0f, 0.0f);
				vertex.uv = Engine::Vector2(u, v);
				mesh.vertices.emplace_back(vertex);
			}
		}

		mesh.indices.reserve(
			static_cast<size_t>(gridSize) *
			static_cast<size_t>(gridSize) * 6);
		for (uint32_t z = 0; z < gridSize; ++z) {
			for (uint32_t x = 0; x < gridSize; ++x) {

				const uint32_t row = gridSize + 1;
				const uint32_t i0 = z * row + x;
				const uint32_t i1 = i0 + 1;
				const uint32_t i2 = i0 + row;
				const uint32_t i3 = i2 + 1;
				mesh.indices.insert(
					mesh.indices.end(),
					{ i0, i2, i1, i1, i2, i3 });
			}
		}

		const uint32_t halfIndexCount =
			static_cast<uint32_t>(mesh.indices.size() / 2);
		Engine::SubMeshDesc firstSubMesh{};
		firstSubMesh.indexCount = halfIndexCount;
		mesh.subMeshes.emplace_back(firstSubMesh);
		Engine::SubMeshDesc secondSubMesh{};
		secondSubMesh.indexOffset = halfIndexCount;
		secondSubMesh.indexCount =
			static_cast<uint32_t>(mesh.indices.size()) -
			halfIndexCount;
		mesh.subMeshes.emplace_back(secondSubMesh);

		Engine::MeshletBuilder builder{};
		builder.Build(mesh);

		uint32_t previousIndexCount =
			mesh.lods[0].indexCount;
		uint32_t previousMeshletCount =
			mesh.lods[0].meshletCount;
		const uint32_t lod0IndexCount =
			mesh.lods[0].indexCount;
		constexpr std::array<float, Engine::kMeshLODCount>
			maximumIndexRatios = {
				1.0f, 0.45f, 0.12f, 0.04f
		};
		if (previousIndexCount != gridSize * gridSize * 6 ||
			previousMeshletCount == 0) {
			return false;
		}

		for (uint32_t lodIndex = 1;
			lodIndex < Engine::kMeshLODCount;
			++lodIndex) {

			const Engine::MeshLODRange& lod =
				mesh.lods[lodIndex];
			if (lod.indexCount == 0 ||
				lod.indexCount % 3 != 0 ||
				lod.indexCount >= previousIndexCount ||
				static_cast<float>(lod.indexCount) >
				static_cast<float>(lod0IndexCount) *
				maximumIndexRatios[lodIndex] ||
				lod.meshletCount == 0 ||
				lod.meshletCount > previousMeshletCount) {
				std::cerr << "LOD" << lodIndex <<
					" indices=" << lod.indexCount <<
					" meshlets=" << lod.meshletCount <<
					" previousIndices=" << previousIndexCount <<
					" previousMeshlets=" << previousMeshletCount <<
					'\n';
				return false;
			}
			previousIndexCount = lod.indexCount;
			previousMeshletCount = lod.meshletCount;
		}

		if (mesh.lods[Engine::kMeshLODCount - 1].
			indexCount > 64u * 3u) {
			return false;
		}

		return Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
			160.0f, 80.0f, 32.0f) &&
			!Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
				534.1f, 0.1f, 0.1f);
	}

	bool TestBlendStates() {

		struct ExpectedBlendState {

			Engine::BlendMode mode;
			D3D12_BLEND source;
			D3D12_BLEND destination;
			D3D12_BLEND_OP operation;
		};
		constexpr std::array expectedStates = {
			ExpectedBlendState{ Engine::BlendMode::Normal,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Add,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Subtract,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_REV_SUBTRACT },
			ExpectedBlendState{ Engine::BlendMode::Multiply,
				D3D12_BLEND_ZERO, D3D12_BLEND_SRC_COLOR,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Screen,
				D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Premultiplied,
				D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_OP_ADD },
		};
		for (const ExpectedBlendState& expected : expectedStates) {

			D3D12_RENDER_TARGET_BLEND_DESC desc{};
			Engine::BlendState{}.Create(expected.mode, desc);
			if (!desc.BlendEnable ||
				desc.SrcBlend != expected.source ||
				desc.DestBlend != expected.destination ||
				desc.BlendOp != expected.operation ||
				desc.SrcBlendAlpha != D3D12_BLEND_ONE ||
				desc.DestBlendAlpha != D3D12_BLEND_INV_SRC_ALPHA ||
				desc.BlendOpAlpha != D3D12_BLEND_OP_ADD) {

				return false;
			}
		}
		return true;
	}

	bool TestMeshBatchInvalidation() {

		using namespace Engine;
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
		MeshBatchResources rainCache, stageCache;
		rainCache.CaptureBatchIdentity(batch, items, mesh);
		stageCache.CaptureBatchIdentity(batch, stageItems, mesh);
		const uint64_t renderRevision = world.GetRenderDataRevision();
		world.MarkMeshColorModified(rain);
		const uint64_t colorRevision = world.GetMeshColorRevision(rain);
		world.MarkMeshColorModified(rain);
		if (world.GetRenderDataRevision() != renderRevision || world.GetMeshColorRevision(rain) <= colorRevision ||
			world.GetMeshColorRevision(stage) != 0 || !rainCache.MatchesBatch(batch, items, mesh) ||
			!stageCache.MatchesBatch(batch, stageItems, mesh)) {
			return false;
		}
		// 先頭と末尾が同じでも中央のEntityやサブメッシュが異なれば再構築する
		items[1] = &d;
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		items[1] = &b;
		payload.subMeshIndex = 1;
		b.payload = batch.PushPayload(payload);
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		b.payload = a.payload;
		// 描画設定、順序、Worldの違いも同じバッチとして扱わない
		b.material = AssetGUID::New();
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		b.material = {};
		b.receiveShadows = false;
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		b.receiveShadows = true;
		b.surfaceMode = MaterialSurfaceMode::Transparent;
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		b.surfaceMode = MaterialSurfaceMode::Opaque;
		std::swap(items[0], items[1]);
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		std::swap(items[0], items[1]);
		ECSWorld otherWorld(ECSWorldKind::Runtime);
		b.world = &otherWorld;
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		b.world = &world;
		if (!rainCache.MatchesBatch(batch, items, mesh)) { return false; }
		world.MarkRenderDataModified(rain);
		if (rainCache.MatchesBatch(batch, items, mesh) || !stageCache.MatchesBatch(batch, stageItems, mesh)) {
			return false;
		}
		// 全体通知とMeshの再読み込みは確実にキャッシュを無効化する
		rainCache.CaptureBatchIdentity(batch, items, mesh);
		world.MarkRenderDataModified();
		if (rainCache.MatchesBatch(batch, items, mesh) || stageCache.MatchesBatch(batch, stageItems, mesh)) {
			return false;
		}
		rainCache.CaptureBatchIdentity(batch, items, mesh);
		++mesh.reloadGeneration;
		if (rainCache.MatchesBatch(batch, items, mesh)) { return false; }
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
		world.DestroyEntity(rain);
		world.FlushPendingDestroyEntities();
		return world.GetMeshColorRevision(rain) == 0 && world.GetEntityRenderRevision(rain) == 0;
	}
}
