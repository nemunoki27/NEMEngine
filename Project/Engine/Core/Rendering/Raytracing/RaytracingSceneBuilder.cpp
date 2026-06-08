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
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>

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
	AssetDatabase& assetDatabase, MeshRenderBackend* meshBackend,
	const RenderSceneBatch& renderBatch, SceneExecutionContext& context) {

	// 描画用Raytracingが無効でも、Debug/DevelopエディターのGPUピック用TLASは構築できるようにする
	const auto& featureController = graphicsCore.GetDXObject().GetFeatureController();
	const bool canBuildRaytracingScene = featureController.GetSupport().SupportsRayTracingPath() &&
		(graphicsCore.GetDXObject().ShouldBuildRaytracingScene() || context.requireRaytracingSceneForEditorPicking);
	if (!canBuildRaytracingScene) {
		return;
	}
	if (!context.sceneInstance || !meshBackend) {
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
	CollectSceneMeshInstances(renderBatch, context, sceneMeshes);
	if (sceneMeshes.empty()) {
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
	meshBackend->RequestMeshes(graphicsCore, assetDatabase, requiredMeshes);

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	ID3D12GraphicsCommandList6* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();

	// データクリア
	sceneInstanceScratch_.clear();
	sceneSubMeshScratch_.clear();

	// BLASの構築とTLASインスタンスの準備
	std::vector<RaytracingTLASInstance> tlasInstances;
	tlasInstances.reserve(sceneMeshes.size());

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
				}
				// BLASリソースを設定
				blasResource = blas.GetResource();
			}

			uint32_t subMeshDataIndex = static_cast<uint32_t>(sceneSubMeshScratch_.size());

			// サブメッシュデータを構築
			MeshSubMeshShaderData subMeshData{};
			subMeshData.importedBaseColor = importedSubMesh.baseColor;
			// ラスタライズ経路と挙動を揃える:
			// テクスチャ指定あり→解決(見つからなければエラー)、未指定→kNoTexture(シェーダ側でベースカラー使用)、
			// 指定はあるが解決できない→エラーテクスチャ
			AssetID baseColorTextureAsset = MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			if (baseColorTextureAsset) {

				subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, baseColorTextureAsset);
			} else if (MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(*meshResource, src.renderer, subMeshIndex)) {

				// 宣言はあるが見つからない: エラーテクスチャ(空AssetIDの解決でerrorIndexが返る)
				subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, AssetID{});
			} else {

				// テクスチャ未設定: シェーダ側でimportedBaseColor*colorを使う
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

			// 初期値。CPU側のMeshSubMeshShaderDataとHLSLのSubMeshShaderDataは同一レイアウトに保つ
			subMeshData.localMatrix = Matrix4x4::Identity();
			subMeshData.localNormalMatrix = Matrix4x4::Identity();
			subMeshData.color = Color4::White();
			subMeshData.emissiveColor = Color4(0.0f, 0.0f, 0.0f, 0.0f);
			subMeshData.uvMatrix = Matrix4x4::Identity();
			if (hasMesh) {

				const auto& authoring = src.renderer->subMeshes[subMeshIndex];
				subMeshData.color = authoring.color;
				subMeshData.emissiveColor = authoring.emissiveColor;
				subMeshData.metallic = authoring.metallic;
				subMeshData.roughness = authoring.roughness;
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
			instance.mask = 0xFF;
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

	// TLASインスタンスがない場合は処理しない
	if (tlasInstances.empty()) {
		return;
	}

	// バッファ転送
	sceneInstances_.Upload(sceneInstanceScratch_);
	sceneSubMeshes_.Upload(sceneSubMeshScratch_);

	// TLASの構築、必要に応じて更新
	if (firstTLASBuild_ || !tlas_.IsBuilt()) {

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
