#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
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
#include <Engine/Core/Rendering/DebugDraw/Lines/Dimensions/LineRenderer3D.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderDataUtility.h>
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

namespace NEMTests {

	// 球描画の省略引数で呼出先が曖昧にならないことを確認する
	static_assert(requires(Engine::LineRenderer3D& renderer, const Engine::Vector3& center, const Engine::Color4& color) {
		renderer.DrawSphere(center, 1.0f, color);
		renderer.DrawSphere(center, 1.0f, color, 2.0f);
		renderer.DrawSphereGrid(center, 1.0f, color);
		renderer.DrawSphereGrid(center, 1.0f, color, 8, 2.0f);
	});

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

		// 不正なoffsetでもParticleの転送先を変更しない
		std::vector<uint8_t> customData(8, 0x5a);
		const auto initialData = customData;
		Engine::ShaderConstantBufferVariable variable{};
		variable.valueType = D3D_SVT_FLOAT;
		variable.size = sizeof(float);
		variable.offset = (std::numeric_limits<uint32_t>::max)();
		const Engine::Vector4 parameter(2.0f, 0.0f, 0.0f, 0.0f);
		Engine::WriteParticleCustomParameter(customData, variable, parameter);
		if (customData != initialData) return false;
		variable.offset = 5;
		Engine::WriteParticleCustomParameter(customData, variable, parameter);
		if (customData != initialData) return false;
		variable.offset = 4;
		Engine::WriteParticleCustomParameter(customData, variable, parameter);
		float storedValue = 0.0f;
		std::memcpy(&storedValue, customData.data() + 4, sizeof(storedValue));
		if (storedValue != parameter.x || !std::equal(customData.begin(), customData.begin() + 4, initialData.begin())) return false;

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
				{ "lodOrthographic", offsetof(Engine::MeshViewConstants, lodOrthographic) },
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

		// Layer判定の削除を全てのLighting経路で検証する
		struct LightingShaderCase {
			const wchar_t* path;
			const wchar_t* profile;
			const wchar_t* entry;
			Engine::ShaderStage stage;
		};
		const LightingShaderCase lightingCases[]{
			{ L"Lighting/deferredLighting.PS.hlsl", L"ps_6_0", L"main", Engine::ShaderStage::PS },
			{ L"Lighting/deferredLighting.PS.hlsl", L"ps_6_6", L"mainShadowed", Engine::ShaderStage::PS },
			{ L"Primitive/primitive.PS.hlsl", L"ps_6_6", L"main", Engine::ShaderStage::PS },
			{ L"Mesh/MeshPBR/meshPBRTransparent.PS.hlsl", L"ps_6_0", L"mainTransparent", Engine::ShaderStage::PS },
			{ L"Raytracing/reflection.RT.hlsl", L"lib_6_6", L"ReflectionRayGen", Engine::ShaderStage::Lib },
		};
		for (const LightingShaderCase& test : lightingCases) {
			const auto shader = compiler.CompileShader(
				(Engine::RuntimePaths::GetEngineAssetPath("Shaders/Builtin") / test.path).wstring(),
				test.profile, test.entry, test.stage);
			if (!shader.IsValid()) {
				std::cerr << "Lighting shader compile failed: " <<
					std::filesystem::path(test.path).string() << '\n' << shader.diagnostics << '\n';
				return false;
			}
		}
		return true;
	}

	bool TestMeshLODGeneration() {

		// 非表示SubMeshを除いてもLODの対応を崩さない
		std::array<Engine::SubMeshDesc, 3> sparseSubMeshes{};
		sparseSubMeshes[0].lods[2] = { .indexOffset = 10, .indexCount = 3 };
		sparseSubMeshes[1].lods[2] = { .indexOffset = 20, .indexCount = 3 };
		sparseSubMeshes[2].lods[2] = { .indexOffset = 30, .indexCount = 3 };
		const std::array<uint32_t, 2> visibleIndices{ 1, 2 };
		std::array<Engine::RaytracingGeometryShaderData, 3> geometryData{};
		geometryData[0] = { .subMeshDataIndex = 7, .indexOffset = 1, .pickRecordIndex = 8 };
		geometryData[1] = { .subMeshDataIndex = 9, .indexOffset = 2, .pickRecordIndex = 10 };
		geometryData[2].indexOffset = 1234;
		const auto firstMesh = std::span<Engine::RaytracingGeometryShaderData>(geometryData).first(2);
		using Engine::RaytracingSceneGeometryUtility::UpdateGeometryLODOffsets;
		if (!UpdateGeometryLODOffsets(sparseSubMeshes, visibleIndices, 2, firstMesh) ||
			geometryData[0].indexOffset != 20 || geometryData[1].indexOffset != 30 || geometryData[2].indexOffset != 1234 ||
			geometryData[0].subMeshDataIndex != 7 || geometryData[0].pickRecordIndex != 8 ||
			geometryData[1].subMeshDataIndex != 9 || geometryData[1].pickRecordIndex != 10) return false;
		// 不正な対応番号は適用済みのGeometryも変更しない
		const std::array<uint32_t, 2> invalidIndices{ 0, 3 };
		if (UpdateGeometryLODOffsets(sparseSubMeshes, invalidIndices, 2, firstMesh) ||
			UpdateGeometryLODOffsets(sparseSubMeshes, visibleIndices, 2, firstMesh.first(1)) ||
			geometryData[0].indexOffset != 20 || geometryData[1].indexOffset != 30 || geometryData[2].indexOffset != 1234) return false;

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
		// 平行投影のLODは距離で変わらず、投影倍率に応じて切り替わる
		Engine::ResolvedRenderView orthoView = nearView;
		orthoView.perspective.projectionMode = Engine::ResolvedProjectionMode::Orthographic;
		if (ResolveMeshLOD(features, &orthoView, center, 1.0f) != 0 ||
			ComputeLODViewHash(features, &orthoView) == ComputeLODViewHash(features, &nearView)) return false;
		orthoView.perspective.matrices.projectionMatrix.m[0][0] = 0.1f;
		orthoView.perspective.matrices.projectionMatrix.m[1][1] = 0.1f;
		if (ResolveMeshLOD(features, &orthoView, center, 1.0f) != 1) return false;
		orthoView.perspective.matrices.viewMatrix.m[3][2] = 1000.0f;
		if (ResolveMeshLOD(features, &orthoView, center, 1.0f) != 1) return false;
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


}
