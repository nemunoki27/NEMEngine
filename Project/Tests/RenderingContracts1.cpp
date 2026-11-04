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
		if (!Engine::ShaderSourcePath::Resolve("b2995658d93cd4ab").empty()) return false;
		// Cameraの固定パスも製品と同じDXCで検証する
		Engine::DxShaderCompiler compiler;
		compiler.Init();
		const std::pair<const char*, const wchar_t*> shaders[] = {
			{"Rendering/Builtin/Raytracing/AmbientOcclusion/ambientOcclusion.CS.hlsl", L"cs_6_6"},
			{"Rendering/Builtin/Raytracing/AmbientOcclusion/ambientOcclusionFilter.CS.hlsl", L"cs_6_0"},
		};
		for (const auto& [source, profile] : shaders) {
			const auto shader = compiler.CompileShader(Engine::RuntimePaths::GetEngineAssetPath(source).wstring(),
				profile, L"main", Engine::ShaderStage::CS);
			if (!shader.IsValid()) {
				std::cerr << source << '\n' << shader.diagnostics << '\n';
				return false;
			}
		}
		return true;
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
		const auto root = Engine::RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Mesh";
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
			const auto* draw = Engine::FindConstantBuffer(shader.reflection, "MeshDrawConstants");
			if (!draw || draw->size != sizeof(Engine::MeshDrawConstants)) {
				return false;
			}
			const std::pair<const char*, size_t> offsets[]{
				{ "previousViewProjection", offsetof(Engine::MeshViewConstants, previousViewProjection) },
				{ "cullingViewProjection", offsetof(Engine::MeshViewConstants, cullingViewProjection) },
				{ "cullingCameraPos", offsetof(Engine::MeshViewConstants, cullingCameraPos) },
				{ "viewSize", offsetof(Engine::MeshViewConstants, viewSize) },
				{ "renderCameraPos", offsetof(Engine::MeshViewConstants, renderCameraPos) },
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
				"Rendering/Builtin/Primitive/primitive2D.VS.hlsl").wstring(),
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
				(Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin") / test.path).wstring(),
				test.profile, test.entry, test.stage);
			if (!shader.IsValid()) {
				std::cerr << "Lighting shader compile failed: " <<
					std::filesystem::path(test.path).string() << '\n' << shader.diagnostics << '\n';
				return false;
			}
		}
		return true;
	}

	bool TestMeshletGeneration() {

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

		const auto originalIndices = mesh.indices;
		Engine::MeshletBuilder builder{};
		builder.Build(mesh);
		uint32_t triangleCount = 0;
		for (const auto& meshlet : mesh.meshlets) {

			triangleCount += meshlet.primitiveCount;
			if (meshlet.subMeshIndex >= mesh.subMeshes.size()) return false;
		}
		if (triangleCount * 3u != originalIndices.size() || mesh.indices != originalIndices) return false;
		uint32_t meshletCount = 0;
		for (const auto& subMesh : mesh.subMeshes) {

			if (subMesh.meshletOffset != meshletCount || subMesh.meshletCount == 0u) return false;
			meshletCount += subMesh.meshletCount;
		}
		if (meshletCount != mesh.meshlets.size()) return false;
		const auto vertexIndices = mesh.meshletVertexIndices;
		const auto primitiveIndices = mesh.meshletPrimitiveIndices;
		builder.Build(mesh);
		return mesh.meshlets.size() == meshletCount && mesh.indices == originalIndices &&
			mesh.meshletVertexIndices == vertexIndices && mesh.meshletPrimitiveIndices == primitiveIndices;
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
