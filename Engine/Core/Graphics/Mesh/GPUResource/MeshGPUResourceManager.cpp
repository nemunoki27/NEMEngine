#include "MeshGPUResourceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>

//============================================================================
//	MeshGPUResourceManager classMethods
//============================================================================

namespace {

	// テクスチャアセットIDをパスから解決する、見つからない場合はインポートする
	Engine::AssetID ResolveTextureAssetIDFromPath(Engine::AssetDatabase& assetDatabase, const std::string& assetPath) {

		if (assetPath.empty()) {
			return {};
		}
		if (const auto* meta = assetDatabase.FindByPath(assetPath)) {
			return meta->guid;
		}
		return assetDatabase.ImportOrGet(assetPath, Engine::AssetType::Texture);
	}
	// サブメッシュのテクスチャアセットIDを解決する、エディタ編集で上書きされたテクスチャがあればそちらを優先する
	void ResolveSubMeshDefaultTextureAssets(Engine::AssetDatabase& assetDatabase,
		Engine::MeshGPUResource& mesh) {

		for (auto& subMesh : mesh.subMeshes) {

			auto& dst = subMesh.defaultTextureAssets;
			const auto& src = subMesh.defaultTextures;

			dst.baseColorTexture = ResolveTextureAssetIDFromPath(assetDatabase, src.baseColorTexturePath);
			dst.normalTexture = ResolveTextureAssetIDFromPath(assetDatabase, src.normalTexturePath);
			dst.metallicRoughnessTexture = ResolveTextureAssetIDFromPath(assetDatabase, src.metallicRoughnessTexturePath);
			dst.specularTexture = ResolveTextureAssetIDFromPath(assetDatabase, src.specularTexturePath);
			dst.emissiveTexture = ResolveTextureAssetIDFromPath(assetDatabase, src.emissiveTexturePath);
			dst.occlusionTexture = ResolveTextureAssetIDFromPath(assetDatabase, src.occlusionTexturePath);
		}
	}
	// BLASのPrimitiveIndex()と一致する並びでサブメッシュインデックスを並べる
	std::vector<uint32_t> BuildPrimitiveSubMeshTable(const std::vector<Engine::SubMeshDesc>& subMeshes,
		uint32_t indexCount) {

		uint32_t primitiveCount = indexCount / 3;
		std::vector<uint32_t> table(primitiveCount, 0);
		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(subMeshes.size()); ++subMeshIndex) {

			const Engine::SubMeshDesc& subMesh = subMeshes[subMeshIndex];

			// サブメッシュのプリミティブ範囲を求める
			uint32_t firstPrimitive = subMesh.indexOffset / 3;
			uint32_t subMeshPrimitiveCount = subMesh.indexCount / 3;
			for (uint32_t primitiveOffset = 0; primitiveOffset < subMeshPrimitiveCount; ++primitiveOffset) {

				const uint32_t primitiveIndex = firstPrimitive + primitiveOffset;
				if (primitiveIndex < table.size()) {

					table[primitiveIndex] = subMeshIndex;
				}
			}
		}
		return table;
	}
}

Engine::MeshGPUResourceManager::~MeshGPUResourceManager() {

	Finalize();
}

void Engine::MeshGPUResourceManager::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	device_ = graphicsCore.GetDXObject().GetDevice();
	srvDescriptor_ = &graphicsCore.GetSRVDescriptor();

	// メッシュインポートサービスの初期化
	importService_.Init(4);
	initialized_ = true;
}

void Engine::MeshGPUResourceManager::Finalize() {

	if (!initialized_) {
		return;
	}

	importService_.Finalize();
	{
		std::scoped_lock lock(mutex_);
		for (auto& [id, mesh] : gpuMeshes_) {
			mesh.vertexSRV.Release(srvDescriptor_);
			mesh.indexSRV.Release(srvDescriptor_);
			mesh.vertexSubMeshIndexSRV.Release(srvDescriptor_);
			mesh.primitiveSubMeshIndexSRV.Release(srvDescriptor_);
			mesh.meshletSRV.Release(srvDescriptor_);
			mesh.meshletVertexIndexSRV.Release(srvDescriptor_);
			mesh.meshletPrimitiveIndexSRV.Release(srvDescriptor_);
			mesh.skinInfluenceSRV.Release(srvDescriptor_);
		}
		gpuMeshes_.clear();
		requested_.clear();
	}
	device_ = nullptr;
	srvDescriptor_ = nullptr;
	assetDatabase_ = nullptr;
	initialized_ = false;
}

void Engine::MeshGPUResourceManager::BeginFrame(GraphicsCore& graphicsCore) {

	// 初期化されていない場合は初期化する
	if (!initialized_) {

		Init(graphicsCore);
	}
}

void Engine::MeshGPUResourceManager::RequestMesh(AssetDatabase& assetDatabase, AssetID meshAssetID) {

	assetDatabase_ = &assetDatabase;

	// 無効なIDは無視
	if (!meshAssetID) {
		return;
	}

	{
		std::scoped_lock lock(mutex_);
		if (gpuMeshes_.contains(meshAssetID) || requested_.contains(meshAssetID)) {
			return;
		}
	}

	// 実際にロード要求が通ったときだけ
	if (!importService_.RequestLoadAsync(assetDatabase, meshAssetID)) {
		return;
	}

	std::scoped_lock lock(mutex_);
	if (!gpuMeshes_.contains(meshAssetID)) {
		requested_.insert(meshAssetID);
	}
}

void Engine::MeshGPUResourceManager::FlushUploads() {

	// 読み込み待ちのメッシュアセットのうち、GPUにアップロードされていないものをアップロードする
	std::vector<AssetID> pending{};
	{
		std::scoped_lock lock(mutex_);
		pending.reserve(requested_.size());
		for (const AssetID& id : requested_) {
			pending.emplace_back(id);
		}
	}

	for (const AssetID& id : pending) {

		ImportedMeshAsset imported{};
		if (!importService_.TakeImported(id, imported)) {
			continue;
		}

		{
			std::scoped_lock lock(mutex_);
			if (gpuMeshes_.contains(id)) {
				requested_.erase(id);
				continue;
			}
		}

		// GPUにアップロード
		UploadImported(imported);

		// アップロード完了したものは要求リストから削除
		{
			std::scoped_lock lock(mutex_);
			requested_.erase(id);
		}
	}
}

const Engine::MeshGPUResource* Engine::MeshGPUResourceManager::Find(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	auto it = gpuMeshes_.find(meshAssetID);
	if (it == gpuMeshes_.end()) {
		return nullptr;
	}
	return &it->second;
}

void Engine::MeshGPUResourceManager::UploadImported(const ImportedMeshAsset& imported) {

	// リソース情報を設定
	MeshGPUResource mesh{};
	mesh.assetID = imported.assetID;
	mesh.vertexCount = static_cast<uint32_t>(imported.vertices.size());
	mesh.indexCount = static_cast<uint32_t>(imported.indices.size());
	mesh.meshletCount = static_cast<uint32_t>(imported.meshlets.size());
	mesh.isSkinned = imported.isSkinned;
	mesh.boneCount = imported.boneCount;

	mesh.subMeshes = imported.subMeshes;
	if (mesh.subMeshes.empty() && 0 < mesh.indexCount) {
		mesh.subMeshes.emplace_back(SubMeshDesc{ 0, mesh.indexCount });
	}

	// サブメッシュのテクスチャアセットIDを解決
	ResolveSubMeshDefaultTextureAssets(*assetDatabase_, mesh);

	// 頂点SRVリソース
	{
		mesh.vertexSRV.buffer = std::make_unique<DxStructuredBuffer<MeshVertex>>();
		mesh.vertexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.vertices.size()));
		mesh.vertexSRV.buffer->TransferData(imported.vertices);
		auto srvDesc = mesh.vertexSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.vertices.size()));
		srvDescriptor_->CreateSRV(mesh.vertexSRV.srvIndex, mesh.vertexSRV.buffer->GetResource(), srvDesc);
		mesh.vertexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.vertexSRV.srvIndex);
		mesh.vertexSRV.buffer->SetSRVGPUHandle(mesh.vertexSRV.srvGPUHandle);
	}

	// インデックスバッファ
	{
		mesh.indexBuffer.CreateBuffer(device_, static_cast<UINT>(imported.indices.size()));
		mesh.indexBuffer.TransferData(imported.indices);
	}

	// インデックスSRVリソース
	{
		mesh.indexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
		mesh.indexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.indices.size()));
		mesh.indexSRV.buffer->TransferData(imported.indices);
		auto srvDesc = mesh.indexSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.indices.size()));
		srvDescriptor_->CreateSRV(mesh.indexSRV.srvIndex, mesh.indexSRV.buffer->GetResource(), srvDesc);
		mesh.indexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.indexSRV.srvIndex);
		mesh.indexSRV.buffer->SetSRVGPUHandle(mesh.indexSRV.srvGPUHandle);
	}

	// スキニングインフルエンスSRVリソース
	if (mesh.isSkinned && !imported.vertexInfluences.empty()) {
		mesh.skinInfluenceSRV.buffer = std::make_unique<DxStructuredBuffer<VertexInfluence>>();
		mesh.skinInfluenceSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.vertexInfluences.size()));
		mesh.skinInfluenceSRV.buffer->TransferData(imported.vertexInfluences);
		auto srvDesc = mesh.skinInfluenceSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.vertexInfluences.size()));
		srvDescriptor_->CreateSRV(mesh.skinInfluenceSRV.srvIndex, mesh.skinInfluenceSRV.buffer->GetResource(), srvDesc);
		mesh.skinInfluenceSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.skinInfluenceSRV.srvIndex);
		mesh.skinInfluenceSRV.buffer->SetSRVGPUHandle(mesh.skinInfluenceSRV.srvGPUHandle);
	}

	// 頂点サブメッシュインデックスSRVリソース
	{
		mesh.vertexSubMeshIndexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
		mesh.vertexSubMeshIndexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.vertexSubMeshIndices.size()));
		mesh.vertexSubMeshIndexSRV.buffer->TransferData(imported.vertexSubMeshIndices);
		auto srvDesc = mesh.vertexSubMeshIndexSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.vertexSubMeshIndices.size()));
		srvDescriptor_->CreateSRV(mesh.vertexSubMeshIndexSRV.srvIndex, mesh.vertexSubMeshIndexSRV.buffer->GetResource(), srvDesc);
		mesh.vertexSubMeshIndexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.vertexSubMeshIndexSRV.srvIndex);
		mesh.vertexSubMeshIndexSRV.buffer->SetSRVGPUHandle(mesh.vertexSubMeshIndexSRV.srvGPUHandle);
	}

	// PrimitiveIndex()->サブメッシュインデックス参照用
	{
		std::vector<uint32_t> primitiveSubMeshTable = BuildPrimitiveSubMeshTable(mesh.subMeshes, mesh.indexCount);
		mesh.primitiveSubMeshIndexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
		mesh.primitiveSubMeshIndexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(primitiveSubMeshTable.size()));
		mesh.primitiveSubMeshIndexSRV.buffer->TransferData(primitiveSubMeshTable);
		auto srvDesc = mesh.primitiveSubMeshIndexSRV.buffer->GetSRVDesc(static_cast<UINT>(primitiveSubMeshTable.size()));
		srvDescriptor_->CreateSRV(mesh.primitiveSubMeshIndexSRV.srvIndex, mesh.primitiveSubMeshIndexSRV.buffer->GetResource(), srvDesc);
		mesh.primitiveSubMeshIndexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.primitiveSubMeshIndexSRV.srvIndex);
		mesh.primitiveSubMeshIndexSRV.buffer->SetSRVGPUHandle(mesh.primitiveSubMeshIndexSRV.srvGPUHandle);
	}

	// メッシュレットSRVリソース
	if (!imported.meshlets.empty()) {
		mesh.meshletSRV.buffer = std::make_unique<DxStructuredBuffer<MeshletDesc>>();
		mesh.meshletSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.meshlets.size()));
		mesh.meshletSRV.buffer->TransferData(imported.meshlets);
		auto srvDesc = mesh.meshletSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.meshlets.size()));
		srvDescriptor_->CreateSRV(mesh.meshletSRV.srvIndex, mesh.meshletSRV.buffer->GetResource(), srvDesc);
		mesh.meshletSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletSRV.srvIndex);
		mesh.meshletSRV.buffer->SetSRVGPUHandle(mesh.meshletSRV.srvGPUHandle);
	}
	if (!imported.meshletVertexIndices.empty()) {
		mesh.meshletVertexIndexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
		mesh.meshletVertexIndexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.meshletVertexIndices.size()));
		mesh.meshletVertexIndexSRV.buffer->TransferData(imported.meshletVertexIndices);
		auto srvDesc = mesh.meshletVertexIndexSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.meshletVertexIndices.size()));
		srvDescriptor_->CreateSRV(mesh.meshletVertexIndexSRV.srvIndex, mesh.meshletVertexIndexSRV.buffer->GetResource(), srvDesc);
		mesh.meshletVertexIndexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletVertexIndexSRV.srvIndex);
		mesh.meshletVertexIndexSRV.buffer->SetSRVGPUHandle(mesh.meshletVertexIndexSRV.srvGPUHandle);
	}
	if (!imported.meshletPrimitiveIndices.empty()) {
		mesh.meshletPrimitiveIndexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
		mesh.meshletPrimitiveIndexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.meshletPrimitiveIndices.size()));
		mesh.meshletPrimitiveIndexSRV.buffer->TransferData(imported.meshletPrimitiveIndices);
		auto srvDesc = mesh.meshletPrimitiveIndexSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.meshletPrimitiveIndices.size()));
		srvDescriptor_->CreateSRV(mesh.meshletPrimitiveIndexSRV.srvIndex, mesh.meshletPrimitiveIndexSRV.buffer->GetResource(), srvDesc);
		mesh.meshletPrimitiveIndexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletPrimitiveIndexSRV.srvIndex);
		mesh.meshletPrimitiveIndexSRV.buffer->SetSRVGPUHandle(mesh.meshletPrimitiveIndexSRV.srvGPUHandle);
	}

	// GPUリソースを保存
	{
		std::scoped_lock lock(mutex_);
		gpuMeshes_.emplace(imported.assetID, std::move(mesh));
	}
}