#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>

// c++
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_set>
#include <variant>

//============================================================================
//	RaytracingSceneBuilder internal
//============================================================================
namespace {

	template <typename T>
	void CreateImmutableSRV(ID3D12Device* device, Engine::BufferUploadService& uploadService,
		Engine::SRVDescriptor& srvDescriptor, Engine::MeshStructuredHandle<T>& out,
		const std::vector<T>& data, const wchar_t* debugName) {

		if (data.empty()) {
			return;
		}

		out.buffer = std::make_unique<Engine::DxImmutableStructuredBuffer<T>>();
		out.buffer->Create(device, uploadService, std::span<const T>(data.data(), data.size()));
		if (ID3D12Resource* resource = out.buffer->GetResource()) {
			resource->SetName(debugName);
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = out.buffer->GetSRVDesc();
		srvDescriptor.CreateSRV(out.srvIndex, out.buffer->GetResource(), srvDesc);
		out.srvGPUHandle = srvDescriptor.GetGPUHandle(out.srvIndex);
	}

	bool HasValidFillMeshIndices(const std::vector<uint32_t>& indices, size_t vertexCount) {

		if (indices.empty() || indices.size() % 3 != 0) {
			return false;
		}
		for (uint32_t index : indices) {
			if (vertexCount <= index) {
				return false;
			}
		}
		return true;
	}

	// テクスチャを持たない単色サブメッシュのシェーダーデータ、FillMesh/Primitiveで共用する
	Engine::MeshSubMeshShaderData MakeFlatSubMeshData(const Engine::Color4& baseColor) {

		Engine::MeshSubMeshShaderData subMeshData{};
		subMeshData.importedBaseColor = baseColor;
		subMeshData.baseColorTextureIndex = UINT32_MAX;
		subMeshData.normalTextureIndex = UINT32_MAX;
		subMeshData.metallicRoughnessTextureIndex = UINT32_MAX;
		subMeshData.emissiveTextureIndex = UINT32_MAX;
		subMeshData.occlusionTextureIndex = UINT32_MAX;
		subMeshData.specularTextureIndex = UINT32_MAX;
		subMeshData.localMatrix = Engine::Matrix4x4::Identity();
		subMeshData.localNormalMatrix = Engine::Matrix4x4::Identity();
		subMeshData.color = Engine::Color4::White();
		subMeshData.emissiveColor = Engine::Color4(0.0f, 0.0f, 0.0f, 0.0f);
		subMeshData.uvMatrix = Engine::Matrix4x4::Identity();
		subMeshData.roughness = 1.0f;
		return subMeshData;
	}
}

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================
void Engine::RaytracingSceneBuilder::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	// バッファ初期化
	sceneInstances_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	sceneSubMeshes_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	srvDescriptor_ = &graphicsCore.GetSRVDescriptor();
	// 事前にある程度の容量を確保しておく
	sceneInstances_.EnsureCapacity(256);
	sceneSubMeshes_.EnsureCapacity(256);
	sceneInstanceScratch_.reserve(256);
	sceneSubMeshScratch_.reserve(256);

	firstTLASBuild_ = true;
	initialized_ = true;
}

void Engine::RaytracingSceneBuilder::Finalize() {

	if (!initialized_) {
		return;
	}

	sceneInstances_.Release();
	sceneSubMeshes_.Release();
	sceneInstanceScratch_.clear();
	sceneSubMeshScratch_.clear();
	scenePickRecords_.clear();

	textureKeyCache_.clear();
	textureDescriptorIndexCache_.clear();

	blases_.clear();
	dynamicBlases_.clear();
	for (auto& pair : fillMeshRTResources_) {
		pair.second.Release(srvDescriptor_);
	}
	fillMeshRTResources_.clear();
	meshBlasGeneration_.clear();
	srvDescriptor_ = nullptr;
	firstTLASBuild_ = true;
	initialized_ = false;
	builtThisFrame_ = false;
	builtSceneInstanceID_ = {};
}

void Engine::RaytracingSceneBuilder::BeginFrame(GraphicsCore& graphicsCore) {

	if (!initialized_) {
		Init(graphicsCore);
	}

	// フラグリセット
	builtThisFrame_ = false;
	builtSceneInstanceID_ = {};
}

void Engine::RaytracingSceneBuilder::BuildForScene(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, MeshRenderBackend* meshBackend, PrimitiveGeometryManager* primitiveGeometryManager,
	const RenderSceneBatch& renderBatch, SceneExecutionContext& context) {

	// 描画用Raytracingが無効でも、Debug/DevelopエディターのGPUピック用TLASは構築できるようにする
	const auto& featureController = graphicsCore.GetDXObject().GetFeatureController();
	const bool canBuildRaytracingScene = featureController.GetSupport().SupportsRayTracingPath() &&
		(graphicsCore.GetDXObject().ShouldBuildRaytracingScene() || context.requireRaytracingSceneForEditorPicking);
	if (!canBuildRaytracingScene) {
		return;
	}
	if (!context.sceneInstance) {
		return;
	}

	// すでに同一シーンインスタンスで構築している場合は、構築済みのシーン情報を渡す
	if (builtThisFrame_ && builtSceneInstanceID_ == context.sceneInstance->instanceID) {
		PublishBuiltScene(context);
		return;
	}

	scenePickRecords_.clear();

	// シーン内の可視メッシュインスタンスを収集する
	std::vector<CollectedMeshInstance> sceneMeshes;
	if (meshBackend) {
		CollectSceneMeshInstances(renderBatch, context, sceneMeshes);
	}
	std::vector<CollectedFillMeshInstance> sceneFillMeshes;
	CollectSceneFillMeshInstances(renderBatch, context, sceneFillMeshes);
	std::vector<CollectedPrimitiveInstance> scenePrimitives;
	if (primitiveGeometryManager) {
		CollectScenePrimitiveInstances(renderBatch, context, scenePrimitives);
	}
	if (sceneMeshes.empty() && sceneFillMeshes.empty() && scenePrimitives.empty()) {
		return;
	}

	// 必要メッシュを一度だけ要求
	std::unordered_set<AssetID> requiredMeshSet{};
	requiredMeshSet.reserve(sceneMeshes.size());
	for (const CollectedMeshInstance& instance : sceneMeshes) {
		if (instance.meshAssetID) {
			requiredMeshSet.insert(instance.meshAssetID);
		}
	}

	// メッシュリソースを要求
	std::vector<AssetID> requiredMeshes{};
	requiredMeshes.reserve(requiredMeshSet.size());
	for (const AssetID& meshAssetID : requiredMeshSet) {

		requiredMeshes.emplace_back(meshAssetID);
	}
	if (meshBackend && !requiredMeshes.empty()) {
		meshBackend->RequestMeshes(graphicsCore, assetDatabase, requiredMeshes);
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	ID3D12GraphicsCommandList6* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	BufferUploadService& uploadService = graphicsCore.GetBufferUploadService();

	// データクリア
	sceneInstanceScratch_.clear();
	sceneSubMeshScratch_.clear();

	// BLASの構築とTLASインスタンスの準備
	std::vector<RaytracingTLASInstance> tlasInstances;
	tlasInstances.reserve(sceneMeshes.size() + sceneFillMeshes.size());

	// BLASリソースを新規/作り直しした場合はTLASのrefitでは反映できないため完全再構築する
	bool requireTlasRebuild = false;

	for (const CollectedMeshInstance& src : sceneMeshes) {

		// メッシュリソースを取得
		const MeshGPUResource* meshResource = meshBackend->FindMeshResource(src.meshAssetID);
		if (!meshResource) {
			continue;
		}
		if (!meshResource->vertexSRV.buffer || !meshResource->indexSRV.buffer) {
			continue;
		}
		if (meshResource->subMeshes.empty()) {
			continue;
		}

		SkinnedVertexSource skinnedSource{};

		// スキンメッシュの頂点ソースを持っているか
		// リソース情報をアウトプットする
		bool hasSkinnedSource = meshResource->isSkinned && meshBackend->FindSkinnedVertexSource(
			src.world, src.entity, src.meshAssetID, skinnedSource);

		// ホットリロードで世代が変わったら、このメッシュの旧世代BLASを破棄してから作り直す
		const uint32_t reloadGeneration = meshResource->reloadGeneration;
		auto generationIt = meshBlasGeneration_.find(src.meshAssetID);
		if (generationIt != meshBlasGeneration_.end() && generationIt->second != reloadGeneration) {

			std::erase_if(blases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
				});
			std::erase_if(dynamicBlases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
				});
		}
		meshBlasGeneration_[src.meshAssetID] = reloadGeneration;

		// サブメッシュ単位でBLASを構築し、TLASインスタンスを準備する
		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(meshResource->subMeshes.size()); ++subMeshIndex) {

			const SubMeshDesc& importedSubMesh = meshResource->subMeshes[subMeshIndex];

			ID3D12Resource* blasResource = nullptr;
			// 頂点のSRVインデックスを設定
			uint32_t vertexDescriptorIndex = meshResource->vertexSRV.srvIndex;
			uint32_t vertexOffset = 0;

			// スキンメッシュの場合
			if (hasSkinnedSource) {

				// 動的BLASのキーを構築
				DynamicBLASKey key{};
				key.world = src.world;
				key.entity = src.entity;
				key.meshAssetID = src.meshAssetID;
				key.subMeshIndex = subMeshIndex;
				key.reloadGeneration = reloadGeneration;

				BottomLevelAccelerationStructure& blas = dynamicBlases_[key];

				// BLASの入力を構築
				RaytracingBLASInput input{};
				input.meshResource = meshResource;
				input.subMeshIndex = subMeshIndex;
				input.indexOffset = importedSubMesh.indexOffset;
				input.indexCount = importedSubMesh.indexCount;
				input.allowUpdate = true;
				input.overrideVertexAddress = skinnedSource.gpuAddress + sizeof(MeshVertex) * static_cast<uint64_t>(skinnedSource.vertexOffset);
				input.overrideVertexCount = meshResource->vertexCount;

				// BLASが構築されていない場合は構築し、すでに構築されている場合は更新する
				if (!blas.IsBuilt()) {

					blas.Build(device, commandList, input);
				} else {

					blas.Update(commandList, input);
				}
				// BLASリソースと頂点SRVインデックス、頂点オフセットを設定
				blasResource = blas.GetResource();
				vertexDescriptorIndex = skinnedSource.srvIndex;
				vertexOffset = skinnedSource.vertexOffset;
			} else {

				// 静的BLASのキーを構築
				BLASKey key{};
				key.meshAssetID = src.meshAssetID;
				key.subMeshIndex = subMeshIndex;
				key.reloadGeneration = reloadGeneration;

				BottomLevelAccelerationStructure& blas = blases_[key];

				// BLASが構築されていない場合は構築する
				if (!blas.IsBuilt()) {

					// BLASの入力を構築
					RaytracingBLASInput input{};
					input.meshResource = meshResource;
					input.subMeshIndex = subMeshIndex;
					input.indexOffset = importedSubMesh.indexOffset;
					input.indexCount = importedSubMesh.indexCount;
					input.allowUpdate = false;
					blas.Build(device, commandList, input);
					// 新規BLASを追加したためTLASは完全再構築する
					requireTlasRebuild = true;
				}
				// BLASリソースを設定
				blasResource = blas.GetResource();
			}

			uint32_t subMeshDataIndex = static_cast<uint32_t>(sceneSubMeshScratch_.size());

			// サブメッシュデータを構築
			MeshSubMeshShaderData subMeshData{};
			subMeshData.importedBaseColor = importedSubMesh.baseColor;
			AssetID baseColorTextureAsset = MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			if (baseColorTextureAsset) {

				subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, baseColorTextureAsset);
			} else if (MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(*meshResource, src.renderer, subMeshIndex)) {

				// 宣言はあるが見つからない:エラーテクスチャ
				subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, AssetID{});
			} else {

				// テクスチャ未設定:シェーダ側でimportedBaseColor*colorを使う
				subMeshData.baseColorTextureIndex = UINT32_MAX;
			}

			bool hasMesh = src.renderer && subMeshIndex < src.renderer->subMeshes.size();

			AssetID normalAsset = MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			AssetID metallicRoughnessAsset = MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			AssetID emissiveAsset = MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			AssetID occlusionAsset = MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			AssetID specularAsset = MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			subMeshData.normalTextureIndex = normalAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, normalAsset) : UINT32_MAX;
			subMeshData.metallicRoughnessTextureIndex = metallicRoughnessAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, metallicRoughnessAsset) : UINT32_MAX;
			subMeshData.emissiveTextureIndex = emissiveAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, emissiveAsset) : UINT32_MAX;
			subMeshData.occlusionTextureIndex = occlusionAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, occlusionAsset) : UINT32_MAX;
			subMeshData.specularTextureIndex = specularAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, specularAsset) : UINT32_MAX;

			// 初期値でCPU側のMeshSubMeshShaderDataとHLSLのSubMeshShaderDataは同一レイアウトに保つ
			subMeshData.localMatrix = Matrix4x4::Identity();
			subMeshData.localNormalMatrix = Matrix4x4::Identity();
			subMeshData.color = Color4::White();
			subMeshData.emissiveColor = Color4(0.0f, 0.0f, 0.0f, 0.0f);
			subMeshData.uvMatrix = Matrix4x4::Identity();
			if (hasMesh) {

				const auto& authoring = src.renderer->subMeshes[subMeshIndex];
				// RTはfixedなSubMeshShaderDataを使うのでparameterOverridesから既知名を取り出して詰める
				const auto& params = authoring.parameterOverrides;
				auto findColor = [&](const char* name, const Color4& fallback) -> Color4 {
					auto it = params.find(name);
					return (it != params.end() && std::holds_alternative<Color4>(it->second.value)) ?
						std::get<Color4>(it->second.value) : fallback;
					};
				auto findFloat = [&](const char* name, float fallback) -> float {
					auto it = params.find(name);
					return (it != params.end() && std::holds_alternative<float>(it->second.value)) ?
						std::get<float>(it->second.value) : fallback;
					};
				// テクスチャindexはMeshDrawPathCommonのresolverがparameterOverridesを見て解決済み
				subMeshData.color = findColor("color", Color4::White());
				subMeshData.emissiveColor = findColor("emissiveColor", Color4(0.0f, 0.0f, 0.0f, 0.0f));
				subMeshData.metallic = findFloat("Metallic", subMeshData.metallic);
				subMeshData.roughness = findFloat("Roughness", subMeshData.roughness);
				subMeshData.uvMatrix = authoring.uvMatrix;
				subMeshData.localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring);
				const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(subMeshData.localMatrix);
				subMeshData.localNormalMatrix = localNormal.matrix;
				subMeshData.localOrientationSign = localNormal.orientationSign;
				subMeshData.sourcePivot = authoring.sourcePivot;
			}
			// サブメッシュデータを追加
			sceneSubMeshScratch_.emplace_back(subMeshData);

			// インスタンスデータを構築
			RaytracingInstanceShaderData instanceShaderData{};
			instanceShaderData.vertexDescriptorIndex = vertexDescriptorIndex;
			instanceShaderData.indexDescriptorIndex = meshResource->indexSRV.srvIndex;
			instanceShaderData.vertexOffset = vertexOffset;
			instanceShaderData.subMeshDataIndex = subMeshDataIndex;
			instanceShaderData.indexOffset = importedSubMesh.indexOffset;
			const uint32_t shaderInstanceIndex = static_cast<uint32_t>(sceneInstanceScratch_.size());

			// インスタンスデータを追加
			sceneInstanceScratch_.emplace_back(instanceShaderData);

			// メッシュピック用のサブメッシュ情報を追加
			MeshSubMeshPickRecord pickRecord{};
			pickRecord.entity = src.entity;
			pickRecord.subMeshIndex = subMeshIndex;
			if (hasMesh) {

				pickRecord.subMeshStableID = src.renderer->subMeshes[subMeshIndex].stableID;
			}
			scenePickRecords_.emplace_back(pickRecord);

			// TLASインスタンスを構築
			RaytracingTLASInstance instance{};
			instance.blas = blasResource;
			instance.instanceID = shaderInstanceIndex;
			instance.hitGroupIndex = 0;
			// CastShadow/CastReflectionに応じて影レイと反射レイの当たり判定を分ける
			instance.mask = kRaytracingMaskAlwaysHit;
			if (src.renderer) {
				if (HasMeshRenderFlag(src.renderer->renderFlags, MeshRenderFlags::CastShadow)) {
					instance.mask |= kRaytracingMaskShadowCaster;
				}
				if (HasMeshRenderFlag(src.renderer->renderFlags, MeshRenderFlags::CastReflection)) {
					instance.mask |= kRaytracingMaskReflectionCaster;
				}
			} else {

				instance.mask |= kRaytracingMaskShadowCaster | kRaytracingMaskReflectionCaster;
			}
			instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			if (hasMesh) {

				instance.worldMatrix =
					MeshSubMeshRuntime::BuildRenderLocalMatrix(src.renderer->subMeshes[subMeshIndex]) *
					src.worldMatrix;
			} else {

				instance.worldMatrix = src.worldMatrix;
			}
			// TLASインスタンスを追加
			tlasInstances.emplace_back(instance);
		}
	}

	for (const CollectedFillMeshInstance& src : sceneFillMeshes) {

		const FillMeshRendererComponent& renderer = *src.renderer;

		FillMeshRTKey key{};
		key.world = src.world;
		key.entity = src.entity;

		FillMeshRaytracingResource& resource = fillMeshRTResources_[key];
		if (resource.builtGeneration != renderer.geometryGeneration || !resource.blas.IsBuilt()) {

			if (!BuildFillMeshRaytracingResource(device, commandList, uploadService, src, resource)) {
				continue;
			}
			// ジオメトリ変化でBLASを作り直したためTLASは完全再構築する
			requireTlasRebuild = true;
		}
		if (!resource.blas.GetResource() || !resource.vertexSRV.buffer || !resource.indexSRV.buffer) {
			continue;
		}

		const uint32_t subMeshDataIndex = static_cast<uint32_t>(sceneSubMeshScratch_.size());

		const MeshSubMeshShaderData subMeshData = MakeFlatSubMeshData(renderer.color);
		sceneSubMeshScratch_.emplace_back(subMeshData);

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = resource.vertexSRV.srvIndex;
		instanceShaderData.indexDescriptorIndex = resource.indexSRV.srvIndex;
		instanceShaderData.vertexOffset = 0;
		instanceShaderData.subMeshDataIndex = subMeshDataIndex;
		instanceShaderData.indexOffset = 0;
		const uint32_t shaderInstanceIndex = static_cast<uint32_t>(sceneInstanceScratch_.size());
		sceneInstanceScratch_.emplace_back(instanceShaderData);

		MeshSubMeshPickRecord pickRecord{};
		pickRecord.entity = src.entity;
		pickRecord.subMeshIndex = 0;
		scenePickRecords_.emplace_back(pickRecord);

		RaytracingTLASInstance instance{};
		instance.blas = resource.blas.GetResource();
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = 0;
		// FillMeshはフラグを持たないため全てのレイに当てる
		instance.mask = kRaytracingMaskAlwaysHit | kRaytracingMaskShadowCaster | kRaytracingMaskReflectionCaster;
		instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.worldMatrix = src.worldMatrix;
		tlasInstances.emplace_back(instance);
	}

	// Primitiveは形状ハッシュ単位で共有BLASを使い、インスタンスごとにTLASへ登録する
	for (const CollectedPrimitiveInstance& src : scenePrimitives) {

		const PrimitiveRendererComponent& renderer = *src.renderer;

		PrimitiveGeometry* geometry = primitiveGeometryManager->GetOrCreate(graphicsCore, src.geometryHash, renderer);
		if (!geometry) {
			continue;
		}
		// BLASを共有ジオメトリから作る、初めて作ったフレームだけTLASを完全再構築する
		const bool wasBuilt = geometry->blasBuilt;
		if (!primitiveGeometryManager->EnsureBLAS(device, commandList, *geometry)) {
			continue;
		}
		if (!wasBuilt) {
			requireTlasRebuild = true;
		}

		const uint32_t subMeshDataIndex = static_cast<uint32_t>(sceneSubMeshScratch_.size());

		const MeshSubMeshShaderData subMeshData = MakeFlatSubMeshData(Color4::White());
		sceneSubMeshScratch_.emplace_back(subMeshData);

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = geometry->vertexBuffer.srvIndex;
		instanceShaderData.indexDescriptorIndex = geometry->indexSRV.srvIndex;
		instanceShaderData.vertexOffset = 0;
		instanceShaderData.subMeshDataIndex = subMeshDataIndex;
		instanceShaderData.indexOffset = 0;
		const uint32_t shaderInstanceIndex = static_cast<uint32_t>(sceneInstanceScratch_.size());
		sceneInstanceScratch_.emplace_back(instanceShaderData);

		MeshSubMeshPickRecord pickRecord{};
		pickRecord.entity = src.entity;
		pickRecord.subMeshIndex = 0;
		scenePickRecords_.emplace_back(pickRecord);

		RaytracingTLASInstance instance{};
		instance.blas = geometry->blas.GetResource();
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = 0;
		// CastShadow/CastReflectionに応じて影レイと反射レイの当たり判定を分ける
		instance.mask = kRaytracingMaskAlwaysHit;
		if (HasMeshRenderFlag(renderer.renderFlags, MeshRenderFlags::CastShadow)) {
			instance.mask |= kRaytracingMaskShadowCaster;
		}
		if (HasMeshRenderFlag(renderer.renderFlags, MeshRenderFlags::CastReflection)) {
			instance.mask |= kRaytracingMaskReflectionCaster;
		}
		instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.worldMatrix = src.worldMatrix;
		tlasInstances.emplace_back(instance);
	}

	// TLASインスタンスがない場合は処理しない
	if (tlasInstances.empty()) {
		return;
	}

	// バッファ転送
	sceneInstances_.Upload(sceneInstanceScratch_);
	sceneSubMeshes_.Upload(sceneSubMeshScratch_);

	// TLASの構築、BLASを新規/作り直しした場合はrefitでは反映できないため完全再構築する
	if (firstTLASBuild_ || !tlas_.IsBuilt() || requireTlasRebuild) {

		tlas_.Build(device, commandList, tlasInstances, true);
		firstTLASBuild_ = false;
	} else {

		tlas_.Update(commandList, tlasInstances);
	}

	// 構築済みにする
	builtThisFrame_ = true;
	builtSceneInstanceID_ = context.sceneInstance->instanceID;

	// 構築したシーン情報をコンテキストに渡す
	PublishBuiltScene(context);
}

void Engine::RaytracingSceneBuilder::CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedMeshInstance>& outInstances) {

	outInstances.clear();

	// シーンインスタンスIDを取得する
	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		// メッシュ描画アイテムで、かつシーンインスタンスIDが一致するものを対象とする
		if (item.backendID != RenderBackendID::Mesh) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		const MeshRenderPayload* payload = renderBatch.GetPayload<MeshRenderPayload>(item);
		if (!payload || !payload->mesh) {
			continue;
		}

		// 収集したメッシュインスタンスの情報を追加する
		CollectedMeshInstance instance{};
		instance.meshAssetID = payload->mesh;
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = nullptr;
		if (item.world && item.world->IsAlive(item.entity)) {
			if (item.world->HasComponent<MeshRendererComponent>(item.entity)) {

				instance.renderer = &item.world->GetComponent<MeshRendererComponent>(item.entity);
			}
		}
		outInstances.emplace_back(instance);
	}
}

void Engine::RaytracingSceneBuilder::CollectSceneFillMeshInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedFillMeshInstance>& outInstances) {

	outInstances.clear();

	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		if (item.backendID != RenderBackendID::FillMesh) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		if (!item.world || !item.world->IsAlive(item.entity)) {
			continue;
		}
		if (!item.world->HasComponent<FillMeshRendererComponent>(item.entity)) {
			continue;
		}

		const FillMeshRendererComponent& renderer = item.world->GetComponent<FillMeshRendererComponent>(item.entity);
		if (renderer.triangleIndices.empty()) {
			continue;
		}

		CollectedFillMeshInstance instance{};
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = &renderer;
		outInstances.emplace_back(instance);
	}
}

void Engine::RaytracingSceneBuilder::CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedPrimitiveInstance>& outInstances) {

	outInstances.clear();

	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		if (item.backendID != RenderBackendID::Primitive) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		if (!item.world || !item.world->IsAlive(item.entity)) {
			continue;
		}
		if (!item.world->HasComponent<PrimitiveRendererComponent>(item.entity)) {
			continue;
		}

		const PrimitiveRendererComponent& renderer = item.world->GetComponent<PrimitiveRendererComponent>(item.entity);

		// 2D描画はスクリーン空間のUIなので影/反射の対象にしない
		if (IsPrimitiveScreen2D(renderer)) {
			continue;
		}

		CollectedPrimitiveInstance instance{};
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = &renderer;
		// batchKeyは上書き分離を含むためBLAS共有には形状ハッシュを使う
		instance.geometryHash = PrimitiveMeshGenerator::ComputeHash(renderer);
		outInstances.emplace_back(instance);
	}
}

bool Engine::RaytracingSceneBuilder::BuildFillMeshRaytracingResource(ID3D12Device8* device,
	ID3D12GraphicsCommandList6* commandList, BufferUploadService& uploadService,
	const CollectedFillMeshInstance& src, FillMeshRaytracingResource& resource) {

	if (!src.renderer || !srvDescriptor_) {
		return false;
	}

	const FillMeshRendererComponent& renderer = *src.renderer;
	if (!HasValidFillMeshIndices(renderer.triangleIndices, renderer.facePositions.size())) {
		return false;
	}

	resource.Release(srvDescriptor_);
	resource.indexBuffer = {};
	resource.blas = {};

	std::vector<MeshVertex> vertices{};
	vertices.reserve(renderer.facePositions.size());
	for (const Vector3& point : renderer.facePositions) {

		MeshVertex vertex{};
		vertex.normal = Vector3(0.0f, 1.0f, 0.0f);
		vertex.tangent = Vector3(1.0f, 0.0f, 0.0f);
		vertex.tangentSign = 1.0f;
		vertex.uv = Vector2::AnyInit(0.0f);
		vertex.position = Vector4(point.x, 0.0f, point.z, 1.0f);
		vertices.emplace_back(vertex);
	}

	CreateImmutableSRV(device, uploadService, *srvDescriptor_,
		resource.vertexSRV, vertices, L"FillMeshRTVertices");
	CreateImmutableSRV(device, uploadService, *srvDescriptor_,
		resource.indexSRV, renderer.triangleIndices, L"FillMeshRTIndices");
	resource.indexBuffer.Create(device, uploadService,
		std::span<const uint32_t>(renderer.triangleIndices.data(), renderer.triangleIndices.size()),
		DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_GENERIC_READ);

	if (!resource.vertexSRV.buffer || !resource.indexSRV.buffer || !resource.indexBuffer.IsCreatedResource()) {
		resource.Release(srvDescriptor_);
		resource.indexBuffer = {};
		return false;
	}

	uploadService.SubmitBatch();

	RaytracingBLASInput input{};
	input.meshResource = nullptr;
	input.customVertexAddress = resource.vertexSRV.buffer->GetResource()->GetGPUVirtualAddress() + offsetof(MeshVertex, position);
	input.customVertexStride = sizeof(MeshVertex);
	input.customVertexCount = static_cast<uint32_t>(vertices.size());
	input.customVertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	input.customIndexAddress = resource.indexBuffer.GetResource()->GetGPUVirtualAddress();
	input.customIndexFormat = resource.indexBuffer.GetFormat();
	input.indexCount = static_cast<uint32_t>(renderer.triangleIndices.size());
	input.allowUpdate = false;

	resource.blas.Build(device, commandList, input);
	resource.builtGeneration = renderer.geometryGeneration;
	return resource.blas.IsBuilt();
}

uint32_t Engine::RaytracingSceneBuilder::ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, AssetID textureAssetID) const {

	// すでに取得済みならそれを返す
	if (auto it = textureDescriptorIndexCache_.find(textureAssetID);
		it != textureDescriptorIndexCache_.end()) {
		return it->second;
	}

	const GPUTextureResource* errorTexture = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	const uint32_t errorIndex = (errorTexture && errorTexture->valid) ? errorTexture->srvIndex : 0;

	const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(graphicsCore, &assetDatabase, textureAssetID);
	if (texture && texture->valid && texture->srvIndex != UINT32_MAX) {

		// エラーテクスチャ以外が解決できている場合はキャッシュする
		if (texture != errorTexture) {
			textureDescriptorIndexCache_[textureAssetID] = texture->srvIndex;
		}
		return texture->srvIndex;
	}

	return errorIndex;
}

void Engine::RaytracingSceneBuilder::PublishBuiltScene(SceneExecutionContext& context) const {

	// RaytracingSceneInstances
	if (sceneInstances_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingSceneInstances",
			.resource = sceneInstances_.GetResource(),
			.gpuAddress = sceneInstances_.GetGPUAddress(),
			.srvGPUHandle = sceneInstances_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneInstanceScratch_.size()),
			.stride = sizeof(RaytracingInstanceShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingSceneInstances",
			.resource = sceneInstances_.GetResource(),
			.gpuAddress = sceneInstances_.GetGPUAddress(),
			.srvGPUHandle = sceneInstances_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneInstanceScratch_.size()),
			.stride = sizeof(RaytracingInstanceShaderData),
			});
	}

	// RaytracingSubMeshes
	if (sceneSubMeshes_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingSubMeshes",
			.resource = sceneSubMeshes_.GetResource(),
			.gpuAddress = sceneSubMeshes_.GetGPUAddress(),
			.srvGPUHandle = sceneSubMeshes_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneSubMeshScratch_.size()),
			.stride = sizeof(MeshSubMeshShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingSubMeshes",
			.resource = sceneSubMeshes_.GetResource(),
			.gpuAddress = sceneSubMeshes_.GetGPUAddress(),
			.srvGPUHandle = sceneSubMeshes_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneSubMeshScratch_.size()),
			.stride = sizeof(MeshSubMeshShaderData),
			});
	}
	context.raytracing.tlasResource = tlas_.GetResource();
	context.raytracing.instanceCount = static_cast<uint32_t>(sceneInstanceScratch_.size());
}
