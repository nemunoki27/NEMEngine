#include "GlobalIlluminationGeometry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveBatchResources.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

//============================================================================
//	GlobalIlluminationGeometry classMethods
//============================================================================
bool Engine::GlobalIlluminationGeometry::PreparePrimitive(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	RenderAssetLibrary& library, ECSWorld* world, Entity entity, const PrimitiveRendererComponent& renderer,
	PrimitiveGeometry& geometry, const MaterialAsset& material, const MaterialParameterSet& parameters,
	const Matrix4x4& worldMatrix, const Matrix4x4& uvMatrix, SkinnedVertexSource& source, ID3D12Resource*& blas) {

	auto* pipeline = FindPipeline(graphicsCore, library, material);
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!pipeline || !camera) return false;
	auto& pointer = primitiveEntries_[{ world, entity }];
	if (!pointer) pointer = std::make_unique<GeometryEntry>();
	auto& entry = *pointer;
	SceneExecutionContext local = context;
	const uint32_t descriptor = geometry.vertexBuffer.srvIndex;
	if (!CopyVertices(graphicsCore, local, entry, descriptor, 0, geometry.vertexCount)) return false;
	PrimitiveInstanceData instance;
	instance.worldMatrix = worldMatrix;
	instance.previousWorldMatrix = worldMatrix;
	instance.uvMatrix = uvMatrix;
	ApplyPrimitiveShapeToInstance(renderer, instance);
	local.bufferRegistry.Register({ .alias = "gInstances", .gpuAddress = Upload(graphicsCore, &instance, sizeof(instance)) });
	struct PrimitiveViewConstants {

		Matrix4x4 viewProjection;
		Matrix4x4 previousViewProjection;
		Vector3 cameraPosition;
		uint32_t frameSerial;
	};
	const PrimitiveViewConstants view{ camera->matrices.viewProjectionMatrix, camera->matrices.viewProjectionMatrix,
		camera->cameraPos, static_cast<uint32_t>(GraphicsFrameState::GetFrameSerial()) };
	local.bufferRegistry.Register({ .alias = "ViewConstants", .gpuAddress = Upload(graphicsCore, &view, sizeof(view)) });
	if (pipeline->layout.IsValid()) {

		const auto resolveTexture = [&](MaterialParameterSemantic semantic, const AssetID& texture) {

			const auto result = RuntimeTextureResolver::ResolveBindless(graphicsCore, context.assetDatabase, texture,
				IsSRGBMaterialTexture(semantic) ? TextureColorSpace::SRGB : TextureColorSpace::Linear,
				semantic == MaterialParameterSemantic::NormalTexture);
			return MaterialParameterBufferBuilder::TextureResolveResult{ result.srvIndex, !result.retry };
		};
		const auto bytes = MaterialParameterBufferBuilder::BuildElement(material.parameters, parameters, pipeline->layout, resolveTexture);
		local.bufferRegistry.Register({ .alias = "MaterialParameters", .gpuAddress = Upload(graphicsCore, bytes.data(), bytes.size()) });
	}
	if (!Dispatch(graphicsCore, local, *pipeline->pipeline, entry, descriptor, 0, geometry.vertexCount, 0)) return false;
	entry.vertices.Transition(*graphicsCore.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	source.gpuAddress = entry.vertices.GetGPUAddress();
	source.srvIndex = entry.vertices.GetSRVIndex();
	source.vertexOffset = 0;
	source.poseGeneration = GraphicsFrameState::GetFrameSerial() + 1;
	source.bufferGeneration = source.gpuAddress;

	// 変形後の頂点でPrimitive専用BLASを更新
	RaytracingBLASGeometryInput input;
	input.vertexAddress = source.gpuAddress + offsetof(MeshVertex, position);
	input.vertexStride = sizeof(MeshVertex);
	input.vertexCount = geometry.vertexCount;
	input.indexAddress = geometry.indexBuffer.GetResource()->GetGPUVirtualAddress();
	input.indexCount = geometry.indexCount;
	input.indexFormat = geometry.indexBuffer.GetFormat();
	const RaytracingBLASInput build{ .geometries = std::span<const RaytracingBLASGeometryInput>(&input, 1), .allowUpdate = true };
	auto& platform = graphicsCore.GetDXObject();
	if (!entry.blas.IsBuilt() || entry.geometryAddress != input.indexAddress) {

		entry.blas.SetRetirementQueue(platform.GetResourceRetirement());
		entry.blas.Build(platform.GetDevice(), platform.GetDxCommand()->GetCommandList(), build);
		FrameProfiler::GetInstance().AddBLASBuild(1);
	} else {

		entry.blas.Update(platform.GetDxCommand()->GetCommandList(), build);
		FrameProfiler::GetInstance().AddBLASRefit(1);
	}
	entry.geometryAddress = input.indexAddress;
	blas = entry.blas.GetResource();
	return true;
}
