#include "GlobalIlluminationGeometry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

//============================================================================
//	GlobalIlluminationGeometry classMethods
//============================================================================
bool Engine::GlobalIlluminationGeometry::PrepareMesh(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	RenderAssetLibrary& library, ECSWorld* world, Entity entity, const MeshRendererComponent& renderer,
	const MeshGPUResource& mesh, std::span<const SubMeshMaterial> subMeshes, const Matrix4x4& worldMatrix,
	SkinnedVertexSource& source) {

	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera) return false;
	std::vector<PipelineEntry*> pipelines(subMeshes.size());
	std::vector<const MaterialAsset*> materials(subMeshes.size());
	bool hasDeformation = false;
	for (size_t index = 0; index < subMeshes.size(); ++index) {

		if (!subMeshes[index].visible) continue;
		const auto id = subMeshes[index].material ? subMeshes[index].material : renderer.material;
		materials[index] = library.LoadMaterial(id);
		if (!materials[index]) continue;
		pipelines[index] = FindPipeline(graphicsCore, library, *materials[index]);
		hasDeformation = hasDeformation || pipelines[index] != nullptr;
	}
	if (!hasDeformation) return false;
	// 元頂点とSkinning済み頂点のどちらも同じ経路へ渡す
	const uint32_t descriptor = source.srvIndex != UINT32_MAX ? source.srvIndex : mesh.vertexSRV.srvIndex;
	const uint32_t offset = source.srvIndex != UINT32_MAX ? source.vertexOffset : 0;
	auto& pointer = meshEntries_[{ world, entity }];
	if (!pointer) pointer = std::make_unique<GeometryEntry>();
	auto& entry = *pointer;
	SceneExecutionContext local = context;
	if (!CopyVertices(graphicsCore, local, entry, descriptor, offset, mesh.vertexCount)) return false;

	// 通常描画と同じWorld行列とSubMesh配置を使用
	MeshInstanceData instance;
	instance.worldMatrix = worldMatrix;
	instance.normalMatrix = BuildSafeMeshNormalMatrix(worldMatrix).matrix;
	instance.subMeshCount = static_cast<uint32_t>(subMeshes.size());
	const auto instanceAddress = Upload(graphicsCore, &instance, sizeof(instance));
	local.bufferRegistry.Register({ .alias = "gMeshInstances", .gpuAddress = instanceAddress });
	std::vector<MeshSubMeshShaderData> subMeshData(subMeshes.size());
	for (size_t index = 0; index < subMeshes.size(); ++index) {

		subMeshData[index].localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(subMeshes[index]);
		subMeshData[index].localNormalMatrix = BuildSafeMeshNormalMatrix(subMeshData[index].localMatrix).matrix;
		subMeshData[index].uvMatrix = MeshSubMeshRuntime::BuildUVMatrix(subMeshes[index]);
	}
	local.bufferRegistry.Register({ .alias = "gSubMeshes",
		.gpuAddress = Upload(graphicsCore, subMeshData.data(), subMeshData.size() * sizeof(MeshSubMeshShaderData)) });
	local.bufferRegistry.Register({ .alias = "gVertexSubMeshIndices",
		.gpuAddress = mesh.vertexSubMeshIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() });
	MeshViewConstants view;
	view.viewProjection = camera->matrices.viewProjectionMatrix;
	view.previousViewProjection = view.viewProjection;
	view.renderCameraPos = camera->cameraPos;
	local.bufferRegistry.Register({ .alias = "ViewConstants", .gpuAddress = Upload(graphicsCore, &view, sizeof(view)) });
	const auto resolveTexture = [&](MaterialParameterSemantic semantic, const AssetID& texture) {

		const auto result = RuntimeTextureResolver::ResolveBindless(graphicsCore, context.assetDatabase, texture,
			IsSRGBMaterialTexture(semantic) ? TextureColorSpace::SRGB : TextureColorSpace::Linear,
			semantic == MaterialParameterSemantic::NormalTexture);
		return MaterialParameterBufferBuilder::TextureResolveResult{ result.srvIndex, !result.retry };
	};
	for (uint32_t index = 0; index < subMeshes.size(); ++index) {

		if (!pipelines[index]) continue;
		auto& pipeline = *pipelines[index];
		if (pipeline.layout.IsValid()) {

			std::vector<uint8_t> values(pipeline.layout.GetSizeInBytes() * subMeshes.size(), 0);
			const auto element = MaterialParameterBufferBuilder::BuildElement(materials[index]->parameters,
				subMeshes[index].materialInstance, pipeline.layout, resolveTexture);
			std::copy(element.begin(), element.end(), values.begin() + pipeline.layout.GetSizeInBytes() * index);
			local.bufferRegistry.Register({ .alias = "gMeshMaterialParameters",
				.gpuAddress = Upload(graphicsCore, values.data(), values.size()) });
		}
		if (!Dispatch(graphicsCore, local, *pipeline.pipeline, entry, descriptor, offset, mesh.vertexCount, index)) return false;
	}
	entry.vertices.Transition(*graphicsCore.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	source.gpuAddress = entry.vertices.GetGPUAddress();
	source.srvIndex = entry.vertices.GetSRVIndex();
	source.vertexOffset = 0;
	source.poseGeneration = GraphicsFrameState::GetFrameSerial() + 1;
	source.bufferGeneration = source.gpuAddress;
	return true;
}
