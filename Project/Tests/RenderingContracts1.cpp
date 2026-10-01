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
#include <optional>
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

	bool TestMeshShaderConstantLayout() {

		Engine::DxShaderCompiler compiler;
		compiler.Init();
		const auto root = Engine::RuntimePaths::GetEngineAssetsRoot() / "Shaders/Builtin/Mesh";
		struct ShaderCase {
			const wchar_t* path;
			const wchar_t* profile;
			Engine::ShaderStage stage;
		};
		const ShaderCase cases[]{
			{ L"Culling/buildIndexedIndirectArgs.CS.hlsl", L"cs_6_0", Engine::ShaderStage::CS },
			{ L"MeshPBR/meshPBRTransparent.VS.hlsl", L"vs_6_0", Engine::ShaderStage::VS },
			{ L"Common/meshGeometry.AS.hlsl", L"as_6_6", Engine::ShaderStage::AS },
			{ L"Common/meshGeometry.MS.hlsl", L"ms_6_6", Engine::ShaderStage::MS },
		};
		for (const auto& test : cases) {

			// DXCが解釈した配置とCPU転送型を照合する
			const auto shader = compiler.CompileShader((root / test.path).wstring(), test.profile, L"main", test.stage);
			const auto* view = Engine::FindConstantBuffer(shader.reflection, "ViewConstants");
			if (!shader.IsValid() || !view || view->size != sizeof(Engine::MeshViewConstants)) {
				std::cerr << "Mesh shader layout case failed: " <<
					std::filesystem::path(test.path).string() <<
					" valid=" << shader.IsValid() <<
					" view=" << (view != nullptr) <<
					" size=" << (view ? view->size : 0u) <<
					" expected=" << sizeof(Engine::MeshViewConstants) << '\n' <<
					shader.diagnostics << '\n';
				return false;
			}
			const std::pair<const char*, size_t> offsets[]{
				{ "previousViewProjection", offsetof(Engine::MeshViewConstants, previousViewProjection) },
				{ "cullingViewProjection", offsetof(Engine::MeshViewConstants, cullingViewProjection) },
				{ "cullingCameraPos", offsetof(Engine::MeshViewConstants, cullingCameraPos) },
				{ "viewSize", offsetof(Engine::MeshViewConstants, viewSize) },
				{ "renderCameraPos", offsetof(Engine::MeshViewConstants, renderCameraPos) },
				{ "lodView", offsetof(Engine::MeshViewConstants, lodView) },
				{ "lodNearClip", offsetof(Engine::MeshViewConstants, lodNearClip) },
			};
			for (const auto& [name, offset] : offsets) {
				const auto found = std::find_if(view->variables.begin(), view->variables.end(),
					[name](const auto& value) { return value.name == name; });
				if (found == view->variables.end() || found->offset != offset) {
					return false;
				}
			}
		}

		// Primitive2Dも3Dと同じView定数配置を使う
		const auto primitive2D = compiler.CompileShader(
			(Engine::RuntimePaths::GetEngineAssetsRoot() /
				"Shaders/Builtin/Primitive/primitive2D.VS.hlsl").wstring(),
			L"vs_6_0", L"main", Engine::ShaderStage::VS);
		const auto* primitiveView = Engine::FindConstantBuffer(
			primitive2D.reflection, "ViewConstants");
		if (!primitive2D.IsValid() || !primitiveView ||
			primitiveView->size != 144u) {

			return false;
		}
		const std::pair<const char*, size_t> primitiveOffsets[]{
			{ "viewProjection", 0u },
			{ "previousViewProjection", 64u },
			{ "cameraPosition", 128u },
			{ "frameSerial", 140u },
		};
		for (const auto& [name, offset] : primitiveOffsets) {
			const auto found = std::find_if(
				primitiveView->variables.begin(), primitiveView->variables.end(),
				[name](const auto& value) { return value.name == name; });
			if (found == primitiveView->variables.end() ||
				found->offset != offset) {

				return false;
			}
		}

		// SM6.0版は直接Heap参照を使わず互換Descriptor Tableを公開する
		const auto meshPixel = compiler.CompileShader(
			(root / "MeshPBR/meshPBR.PS.hlsl").wstring(),
			L"ps_6_0", L"main", Engine::ShaderStage::PS);
		const auto globalTexture = std::find_if(
			meshPixel.reflection.resources.begin(),
			meshPixel.reflection.resources.end(),
			[](const Engine::ShaderResourceBinding& binding) {
				return binding.name == "gNEMGlobalTexture2D" &&
					binding.space == 126u;
			});
		if (!meshPixel.IsValid() ||
			globalTexture == meshPixel.reflection.resources.end() ||
			globalTexture->bindCount != 0 ||
			(meshPixel.reflection.requiresFlags &
				D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING) != 0) {

			return false;
		}
		return true;
	}

	bool TestMeshLODGeneration() {

		// 同じ形状でも描画Cameraの距離に応じてLODを選ぶ
		Engine::GraphicsRuntimeFeatures features;
		features.useMeshLOD = true;
		features.meshLOD0PixelThreshold = 100.0f;
		features.meshLOD1PixelThreshold = 40.0f;
		features.meshLOD2PixelThreshold = 10.0f;
		Engine::ResolvedRenderView nearView;
		nearView.width = nearView.height = 1000;
		nearView.perspective.valid = true;
		Engine::ResolvedRenderView farView = nearView;
		farView.perspective.matrices.viewMatrix.m[3][2] = 1000.0f;
		const Engine::Vector3 center(0.0f, 0.0f, 10.0f);
		using namespace Engine::RaytracingSceneGeometryUtility;
		if (ResolveMeshLOD(features, &nearView, center, 1.0f) != 1 ||
			ResolveMeshLOD(features, &farView, center, 1.0f) != 3 ||
			ComputeLODViewHash(features, &nearView) == ComputeLODViewHash(features, &farView)) return false;
		features.useMeshLOD = false;
		if (ResolveMeshLOD(features, &farView, center, 1.0f) != 0) return false;
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
		Engine::ImportedMeshAsset defaultMesh = mesh;
		builder.Build(defaultMesh);
		if (defaultMesh.indices.size() != mesh.indices.size()) {
			return false;
		}
		for (uint32_t lodIndex = 1;
			lodIndex < Engine::kMeshLODCount; ++lodIndex) {

			if (defaultMesh.lods[lodIndex].indexOffset != 0 ||
				defaultMesh.lods[lodIndex].indexCount !=
					defaultMesh.lods[0].indexCount) {
				return false;
			}
		}
		Engine::MeshImportSettings lodSettings{};
		lodSettings.generateAutomaticLODs = true;
		builder.Build(mesh, lodSettings);

		uint32_t previousIndexCount =
			mesh.lods[0].indexCount;
		uint32_t previousMeshletCount =
			mesh.lods[0].meshletCount;
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

		// 最低LODでも全SubMeshを残す
		for (const Engine::SubMeshDesc& subMesh : mesh.subMeshes) {
			if (subMesh.lods[Engine::kMeshLODCount - 1].indexCount == 0) {
				return false;
			}
		}

		return Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
			160.0f, 80.0f, 32.0f) &&
			!Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
				534.1f, 0.1f, 0.1f);
	}

	bool TestGraphicsFeatureSelection() {

		Engine::GraphicsFeaturePreferences preferences{};
		preferences.allowMeshShader = true;
		preferences.allowInlineRayTracing = true;
		preferences.allowDispatchRays = true;

		Engine::GraphicsFeatureSupport unsupported{};
		const Engine::GraphicsRuntimeFeatures restricted =
			Engine::GraphicsFeatureSelection::Resolve(
				unsupported, preferences);
		if (restricted.useMeshShader ||
			restricted.useInlineRayTracing ||
			restricted.useDispatchRays ||
			!preferences.allowMeshShader ||
			!preferences.allowInlineRayTracing ||
			!preferences.allowDispatchRays) {
			return false;
		}

		Engine::GraphicsFeatureSupport supported{};
		supported.highestShaderModel = D3D_SHADER_MODEL_6_6;
		supported.meshShaderTier = D3D12_MESH_SHADER_TIER_1;
		supported.raytracingTier = D3D12_RAYTRACING_TIER_1_1;
		const Engine::GraphicsRuntimeFeatures enabled =
			Engine::GraphicsFeatureSelection::Resolve(
				supported, preferences);
		if (!enabled.useMeshShader ||
			!enabled.useInlineRayTracing ||
			!enabled.useDispatchRays) {

			return false;
		}

		// RT Tierだけでなく各経路が必要とするShader Modelも確認する
		Engine::GraphicsFeatureSupport shaderModel60 = supported;
		shaderModel60.highestShaderModel = D3D_SHADER_MODEL_6_0;
		const Engine::GraphicsRuntimeFeatures shaderModel60Features =
			Engine::GraphicsFeatureSelection::Resolve(
				shaderModel60, preferences);
		if (shaderModel60Features.useMeshShader ||
			shaderModel60Features.useInlineRayTracing ||
			shaderModel60Features.useDispatchRays) {

			return false;
		}

		Engine::GraphicsFeatureSupport dispatchRays = supported;
		dispatchRays.highestShaderModel = D3D_SHADER_MODEL_6_3;
		const Engine::GraphicsRuntimeFeatures dispatchRaysFeatures =
			Engine::GraphicsFeatureSelection::Resolve(
				dispatchRays, preferences);
		return !dispatchRaysFeatures.useMeshShader &&
			!dispatchRaysFeatures.useInlineRayTracing &&
			dispatchRaysFeatures.useDispatchRays;
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
		MeshBatchResources reusedCache;
		RenderItem reusedItem;
		reusedItem.world = &*reusedWorld;
		reusedItem.entity = SceneAuthoring::CreateGameObject(*reusedWorld, "Reused");
		reusedItem.payload = reusedBatch.PushPayload(MeshRenderPayload{});
		const std::array<const RenderItem*, 1> reusedItems{ &reusedItem };
		reusedCache.CaptureBatchIdentity(reusedBatch, reusedItems, reusedMesh);
		reusedWorld.reset();
		reusedWorld.emplace(ECSWorldKind::Runtime);
		reusedItem.entity = SceneAuthoring::CreateGameObject(*reusedWorld, "Reused");
		if (reusedCache.MatchesBatch(reusedBatch, reusedItems, reusedMesh) || reusedCache.RefreshMaterialColors() != 0) return false;
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
