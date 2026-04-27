#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Scene/Instance/SceneInstanceManager.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderBackend.h>

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

	// レイトレーシングが有効でない場合やシーンインスタンスがない場合は処理しない
	if (!graphicsCore.GetDXObject().ShouldBuildRaytracingScene()) {
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
			AssetID baseColorTextureAsset = MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(*meshResource, src.renderer, subMeshIndex);
			subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, baseColorTextureAsset);

			bool hasMesh = src.renderer && subMeshIndex < src.renderer->subMeshes.size();

			// 初期値
			subMeshData.localMatrix = Matrix4x4::Identity();
			subMeshData.color = Color4::White();
			subMeshData.uvMatrix = Matrix4x4::Identity();
			if (hasMesh) {

				const auto& authoring = src.renderer->subMeshes[subMeshIndex];
				subMeshData.color = authoring.color;
				subMeshData.uvMatrix = authoring.uvMatrix;
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

				instance.worldMatrix = src.renderer->subMeshes[subMeshIndex].worldMatrix;
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

	// フォールバックテクスチャのSRVインデックスを取得する
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	const uint32_t fallbackSRVIndex =
		(fallback && fallback->valid && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;

	if (!textureAssetID) {
		return fallbackSRVIndex;
	}

	// すでに取得済みならそれを返す
	if (auto it = textureDescriptorIndexCache_.find(textureAssetID);
		it != textureDescriptorIndexCache_.end()) {
		return it->second;
	}

	// パス文字列を一度だけ解決してキャッシュ
	std::string key{};
	if (auto it = textureKeyCache_.find(textureAssetID); it != textureKeyCache_.end()) {

		key = it->second;
	} else {

		std::filesystem::path fullPath = assetDatabase.ResolveFullPath(textureAssetID);
		if (fullPath.empty()) {
			return fallbackSRVIndex;
		}
		key = fullPath.generic_string();
		textureKeyCache_.emplace(textureAssetID, key);
	}
	graphicsCore.GetTextureUploadService().RequestTextureFile(key, key);
	if (const auto* texture = graphicsCore.GetTextureUploadService().GetTexture(key)) {
		if (texture->valid && texture->srvIndex != UINT32_MAX) {

			textureDescriptorIndexCache_[textureAssetID] = texture->srvIndex;
			return texture->srvIndex;
		}
	}
	return fallbackSRVIndex;
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