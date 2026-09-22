#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include "ParticleDrawConstants.h"
#include "ParticleRenderDataUtility.h"
#include "ParticleTrailDataBuilder.h"

#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <unordered_map>

//============================================================================
//	ParticleRenderBackend internal
//============================================================================
namespace {

	Engine::PrimitiveRendererComponent MakeShapeComponent(const Engine::ParticleRenderSettings& settings) {

		Engine::PrimitiveRendererComponent shape{};
		shape.type = settings.shape;
		shape.plane = settings.plane;
		shape.crossPlane = settings.crossPlane;
		shape.ring = settings.ring;
		shape.cylinder = settings.cylinder;
		shape.sphere = settings.sphere;
		shape.hemisphere = settings.hemisphere;
		shape.cube = settings.cube;
		return shape;
	}
}

bool Engine::ParticleRenderBackend::DrawParametricShapePath(const RenderDrawContext& context, const RenderItem* item,
	const IParticleParametricShape& parametric, const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
	const MaterialParameterSet* materialInstance,
	D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
	D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
	uint32_t instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS viewAddress) {

	// 専用MSパイプラインを解決する、解決できなければ共有ジオメトリへ落とす
	const PipelineVariantDesc* variant = nullptr;
	const PipelineState* pipelineState = BackendDrawCommon::ResolveComposedGraphicsPipeline(
		context, *resolvedPass.pass, parametric.GetPipeline(), PipelineVariantKind::GraphicsMesh, &variant);
	if (!pipelineState || !variant || variant->kind != PipelineVariantKind::GraphicsMesh) {
		return false;
	}

	// 分割数の定数バッファを確保する
	ID3D12Device* device = context.graphicsCore->GetDXObject().GetDevice();
	ParticleShapeConstants shapeConstants{};
	shapeConstants.divide = static_cast<uint32_t>(std::clamp(
		parametric.GetDivide(settings), 3, kMaxPrimitiveDivide));
	shapeConstants.uvMode = settings.shape == PrimitiveType::Cylinder ?
		static_cast<uint32_t>(settings.cylinder.uvMode) : 0;
	shapeConstants.cap = settings.shape == PrimitiveType::Cylinder ?
		static_cast<uint32_t>(settings.cylinder.cap) : 0;
	shapeConstants.heightDivide = settings.shape == PrimitiveType::Cylinder ?
		static_cast<uint32_t>(std::clamp(settings.cylinder.heightDivide, 2, kMaxPrimitiveDivide)) : 1;
	if ((shapeConstants.heightDivide & 1u) != 0u && shapeConstants.heightDivide < kMaxPrimitiveDivide) {
		++shapeConstants.heightDivide;
	}
	const PostProcessConstantBufferAllocation shapeAlloc = constantBufferAllocator_.AllocateAndUpload(device, shapeConstants);

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAddress);
	}
	if (perDrawBindCache_.Has(shapeConstantsCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(shapeConstantsCBVSlot_), shapeAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(geometrySRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(geometrySRVSlot_), geometryAddress, {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_), materialsAddress, {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && customParametersAddress != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			customParametersAddress, {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, materialInstance, commandList);
	}

	// 1グループ64三角形で全粒子分をDispatchMeshする
	uint32_t triangleCount = shapeConstants.divide * 2;
	if (settings.shape == PrimitiveType::Cylinder) {

		triangleCount *= shapeConstants.heightDivide;
		switch (settings.cylinder.cap) {
		case PrimitiveCylinderCap::Top:
		case PrimitiveCylinderCap::Bottom:
			triangleCount += shapeConstants.divide;
			break;
		case PrimitiveCylinderCap::Both:
			triangleCount += shapeConstants.divide * 2;
			break;
		case PrimitiveCylinderCap::None:
		default:
			break;
		}
	}
	const uint32_t groupCount = (triangleCount + kParticleMeshGroupTriangles - 1) / kParticleMeshGroupTriangles;
	commandList->DispatchMesh(groupCount, instanceCount, 1);
	return true;
}

bool Engine::ParticleRenderBackend::DrawModelMeshPath(const RenderDrawContext& context, const RenderItem* item,
	const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
	const MaterialParameterSet* materialInstance,
	D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
	D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
	uint32_t instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS viewAddress) {

	// Model粒子、メッシュのGPUリソースを引きインスタンシング描画する
	const MeshGPUResource* meshResource = meshResourceManager_.Find(settings.model);
	if (!meshResource) {

		meshResourceManager_.RequestMesh(*context.assetDatabase, settings.model);
		meshResourceManager_.FlushUploads();
		meshResource = meshResourceManager_.Find(settings.model);
	}
	if (!meshResource || !meshResource->vertexSRV.buffer || meshResource->indexCount == 0) {
		return false;
	}

	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	if (!pipelineState) {
		return false;
	}

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAddress);
	}
	if (perDrawBindCache_.Has(verticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
			meshResource->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(), {});
	}
	if (perDrawBindCache_.Has(geometrySRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(geometrySRVSlot_), geometryAddress, {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_), materialsAddress, {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && customParametersAddress != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			customParametersAddress, {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, materialInstance, commandList);
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_INDEX_BUFFER_VIEW indexBufferView = meshResource->indexBuffer.GetIndexBufferView();
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(meshResource->indexCount, instanceCount, 0, 0, 0);
	return true;
}

void Engine::ParticleRenderBackend::DrawSharedGeometryPath(const RenderDrawContext& context, const RenderItem* item,
	const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
	const MaterialParameterSet* materialInstance,
	D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
	D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
	uint32_t instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS viewAddress) {

	// 粒子が共有する形状ジオメトリを取得する、無ければ生成する
	GraphicsCore& graphicsCore = *context.graphicsCore;
	const PrimitiveRendererComponent shape = MakeShapeComponent(settings);
	const uint64_t geometryHash = PrimitiveMeshGenerator::ComputeHash(shape);
	const PrimitiveGeometry* geometry = geometryManager_.GetOrCreate(graphicsCore, geometryHash, shape);
	if (!geometry || geometry->indexCount == 0 || !geometry->vertexBuffer.buffer) {
		return;
	}

	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	if (!pipelineState) {
		return;
	}

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAddress);
	}
	if (perDrawBindCache_.Has(verticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
			geometry->vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress(), {});
	}
	if (perDrawBindCache_.Has(geometrySRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(geometrySRVSlot_), geometryAddress, {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_), materialsAddress, {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && customParametersAddress != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			customParametersAddress, {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, materialInstance, commandList);
	}

	// 共有インデックスバッファでインスタンシング描画する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_INDEX_BUFFER_VIEW indexBufferView = geometry->indexBuffer.GetIndexBufferView();
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(geometry->indexCount, instanceCount, 0, 0, 0);
}
