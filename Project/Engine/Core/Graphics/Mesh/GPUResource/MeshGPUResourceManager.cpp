#include "MeshGPUResourceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>

// c++
#include <algorithm>
#include <cmath>

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
	// メッシュ全体のバウンディング球を構築する
	void CalcMeshBounds(const std::vector<Engine::MeshVertex>& vertices, Engine::Vector3& outCenter, float& outRadius) {

		// 空メッシュはカリングできるBoundsを持たないので半径0にする
		if (vertices.empty()) {
			outCenter = Engine::Vector3::AnyInit(0.0f);
			outRadius = 0.0f;
			return;
		}

		auto toPosition3 = [](const Engine::Vector4& position) {
			return Engine::Vector3(position.x, position.y, position.z);
			};

		Engine::Vector3 minPos = toPosition3(vertices.front().position);
		Engine::Vector3 maxPos = minPos;
		// メッシュ全体のAABBを求める
		for (const Engine::MeshVertex& vertex : vertices) {

			const Engine::Vector3 pos = toPosition3(vertex.position);
			minPos.x = (std::min)(minPos.x, pos.x);
			minPos.y = (std::min)(minPos.y, pos.y);
			minPos.z = (std::min)(minPos.z, pos.z);
			maxPos.x = (std::max)(maxPos.x, pos.x);
			maxPos.y = (std::max)(maxPos.y, pos.y);
			maxPos.z = (std::max)(maxPos.z, pos.z);
		}

		outCenter = (minPos + maxPos) * 0.5f;
		outRadius = 0.0f;
		// AABB中心から最遠点までを球半径にする
		for (const Engine::MeshVertex& vertex : vertices) {

			const Engine::Vector3 pos = toPosition3(vertex.position);
			outRadius = (std::max)(outRadius, Engine::Vector3::Length(pos - outCenter));
		}
	}

	int16_t QuantizeSnorm16(float value) {

		// 法線のOct成分を16bit符号付き正規化値へ丸める
		value = (std::clamp)(value, -1.0f, 1.0f);
		return static_cast<int16_t>(std::round(value * 32767.0f));
	}

	uint32_t EncodeOctNormal(const Engine::Vector3& normal) {

		// 3成分法線を2成分のOctahedral表現へ変換する
		Engine::Vector3 n = normal.Normalize();
		const float length = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
		if (length <= 0.00001f) {
			return 0;
		}

		float x = n.x / length;
		float y = n.y / length;
		if (n.z < 0.0f) {

			// 裏面側の半球を2D平面へ折り返す
			const float oldX = x;
			x = (1.0f - std::abs(y)) * (oldX >= 0.0f ? 1.0f : -1.0f);
			y = (1.0f - std::abs(oldX)) * (y >= 0.0f ? 1.0f : -1.0f);
		}

		const uint16_t packedX = static_cast<uint16_t>(QuantizeSnorm16(x));
		const uint16_t packedY = static_cast<uint16_t>(QuantizeSnorm16(y));
		return static_cast<uint32_t>(packedX) | (static_cast<uint32_t>(packedY) << 16);
	}

	std::vector<Engine::MeshPackedVertex> BuildPackedVertices(const std::vector<Engine::MeshVertex>& vertices) {

		// MeshShaderで読む法線だけを圧縮した頂点配列を作る
		std::vector<Engine::MeshPackedVertex> packed{};
		packed.reserve(vertices.size());
		for (const Engine::MeshVertex& vertex : vertices) {

			Engine::MeshPackedVertex dst{};
			dst.normalOct = EncodeOctNormal(vertex.normal);
			dst.uv = vertex.uv;
			dst.position = vertex.position;
			packed.emplace_back(dst);
		}
		return packed;
	}

	bool CanPackMeshletVertexIndices(const std::vector<uint32_t>& indices) {

		// 16bitに収まる場合だけ2要素/uint32_tへ圧縮する
		for (uint32_t index : indices) {
			if (0xFFFFu < index) {
				return false;
			}
		}
		return true;
	}

	std::vector<uint32_t> BuildPackedMeshletVertexIndices(const std::vector<uint32_t>& indices) {

		// 16bitのメッシュレット頂点Indexを2つずつuint32_tへ詰める
		std::vector<uint32_t> packed((indices.size() + 1) / 2, 0);
		for (size_t i = 0; i < indices.size(); ++i) {

			const uint32_t value = indices[i] & 0xFFFFu;
			if ((i & 1u) == 0) {
				packed[i >> 1] |= value;
			} else {
				packed[i >> 1] |= value << 16;
			}
		}
		return packed;
	}

	std::vector<uint16_t> BuildIndex16(const std::vector<uint32_t>& indices) {

		// IBV用に32bit Indexを16bitへ変換する
		std::vector<uint16_t> packed{};
		packed.reserve(indices.size());
		for (uint32_t index : indices) {
			packed.emplace_back(static_cast<uint16_t>(index));
		}
		return packed;
	}

	std::vector<Engine::MeshletDrawDesc> BuildMeshletDrawDescs(const std::vector<Engine::MeshletDesc>& meshlets) {

		// MSが読む範囲情報だけを抜き出す
		std::vector<Engine::MeshletDrawDesc> result{};
		result.reserve(meshlets.size());
		for (const Engine::MeshletDesc& meshlet : meshlets) {

			Engine::MeshletDrawDesc desc{};
			desc.vertexOffset = meshlet.vertexOffset;
			desc.vertexCount = meshlet.vertexCount;
			desc.primitiveOffset = meshlet.primitiveOffset;
			desc.primitiveCount = meshlet.primitiveCount;
			desc.subMeshIndex = meshlet.subMeshIndex;
			result.emplace_back(desc);
		}
		return result;
	}

	std::vector<Engine::MeshletBounds> BuildMeshletBounds(const std::vector<Engine::MeshletDesc>& meshlets) {

		// ASのメッシュレット単位カリングで使うBoundsだけを分離する
		std::vector<Engine::MeshletBounds> result{};
		result.reserve(meshlets.size());
		for (const Engine::MeshletDesc& meshlet : meshlets) {

			result.emplace_back(Engine::MeshletBounds{ meshlet.boundsCenter, meshlet.boundsRadius,
				meshlet.coneAxis, meshlet.coneCutoff });
		}
		return result;
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
			mesh.packedVertexSRV.Release(srvDescriptor_);
			mesh.indexSRV.Release(srvDescriptor_);
			mesh.vertexSubMeshIndexSRV.Release(srvDescriptor_);
			mesh.primitiveSubMeshIndexSRV.Release(srvDescriptor_);
			mesh.meshletSRV.Release(srvDescriptor_);
			mesh.meshletDrawSRV.Release(srvDescriptor_);
			mesh.meshletBoundsSRV.Release(srvDescriptor_);
			mesh.meshletVertexIndexSRV.Release(srvDescriptor_);
			mesh.packedMeshletVertexIndexSRV.Release(srvDescriptor_);
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
	// インスタンス単位カリングで使用するメッシュ全体Boundsを作る
	CalcMeshBounds(imported.vertices, mesh.boundsCenter, mesh.boundsRadius);

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

	// 描画用圧縮頂点SRVリソース
	{
		// MeshShader側の帯域削減用に、法線を圧縮した頂点バッファも作る
		std::vector<MeshPackedVertex> packedVertices = BuildPackedVertices(imported.vertices);
		mesh.packedVertexSRV.buffer = std::make_unique<DxStructuredBuffer<MeshPackedVertex>>();
		mesh.packedVertexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(packedVertices.size()));
		mesh.packedVertexSRV.buffer->TransferData(packedVertices);
		auto srvDesc = mesh.packedVertexSRV.buffer->GetSRVDesc(static_cast<UINT>(packedVertices.size()));
		srvDescriptor_->CreateSRV(mesh.packedVertexSRV.srvIndex, mesh.packedVertexSRV.buffer->GetResource(), srvDesc);
		mesh.packedVertexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.packedVertexSRV.srvIndex);
		mesh.packedVertexSRV.buffer->SetSRVGPUHandle(mesh.packedVertexSRV.srvGPUHandle);
	}

	// インデックスバッファ
	{
		// 16bitに収まるメッシュはIBVだけ16bit化して帯域を減らす
		const bool useIndex16 = CanPackMeshletVertexIndices(imported.indices);
		mesh.indexBuffer.CreateBuffer(device_, static_cast<UINT>(imported.indices.size()),
			useIndex16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);
		if (useIndex16) {
			mesh.indexBuffer.TransferData(BuildIndex16(imported.indices));
		} else {
			mesh.indexBuffer.TransferData(imported.indices);
		}
	}

	// インデックスSRVリソース
	{
		// シェーダ側では32bit Indexとして読むため、SRVは従来どおり32bitを保持する
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
		// 互換用のフルDescも保持しておく
		mesh.meshletSRV.buffer = std::make_unique<DxStructuredBuffer<MeshletDesc>>();
		mesh.meshletSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.meshlets.size()));
		mesh.meshletSRV.buffer->TransferData(imported.meshlets);
		auto srvDesc = mesh.meshletSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.meshlets.size()));
		srvDescriptor_->CreateSRV(mesh.meshletSRV.srvIndex, mesh.meshletSRV.buffer->GetResource(), srvDesc);
		mesh.meshletSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletSRV.srvIndex);
		mesh.meshletSRV.buffer->SetSRVGPUHandle(mesh.meshletSRV.srvGPUHandle);

		// MSが使う範囲情報だけを分離して読み込み量を減らす
		std::vector<MeshletDrawDesc> drawDescs = BuildMeshletDrawDescs(imported.meshlets);
		mesh.meshletDrawSRV.buffer = std::make_unique<DxStructuredBuffer<MeshletDrawDesc>>();
		mesh.meshletDrawSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(drawDescs.size()));
		mesh.meshletDrawSRV.buffer->TransferData(drawDescs);
		auto drawSrvDesc = mesh.meshletDrawSRV.buffer->GetSRVDesc(static_cast<UINT>(drawDescs.size()));
		srvDescriptor_->CreateSRV(mesh.meshletDrawSRV.srvIndex, mesh.meshletDrawSRV.buffer->GetResource(), drawSrvDesc);
		mesh.meshletDrawSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletDrawSRV.srvIndex);
		mesh.meshletDrawSRV.buffer->SetSRVGPUHandle(mesh.meshletDrawSRV.srvGPUHandle);

		// ASで先にカリングできるようBounds/NormalConeだけを分離する
		std::vector<MeshletBounds> bounds = BuildMeshletBounds(imported.meshlets);
		mesh.meshletBoundsSRV.buffer = std::make_unique<DxStructuredBuffer<MeshletBounds>>();
		mesh.meshletBoundsSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(bounds.size()));
		mesh.meshletBoundsSRV.buffer->TransferData(bounds);
		auto boundsSrvDesc = mesh.meshletBoundsSRV.buffer->GetSRVDesc(static_cast<UINT>(bounds.size()));
		srvDescriptor_->CreateSRV(mesh.meshletBoundsSRV.srvIndex, mesh.meshletBoundsSRV.buffer->GetResource(), boundsSrvDesc);
		mesh.meshletBoundsSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletBoundsSRV.srvIndex);
		mesh.meshletBoundsSRV.buffer->SetSRVGPUHandle(mesh.meshletBoundsSRV.srvGPUHandle);
	}
	if (!imported.meshletVertexIndices.empty()) {
		mesh.meshletVertexIndexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
		mesh.meshletVertexIndexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(imported.meshletVertexIndices.size()));
		mesh.meshletVertexIndexSRV.buffer->TransferData(imported.meshletVertexIndices);
		auto srvDesc = mesh.meshletVertexIndexSRV.buffer->GetSRVDesc(static_cast<UINT>(imported.meshletVertexIndices.size()));
		srvDescriptor_->CreateSRV(mesh.meshletVertexIndexSRV.srvIndex, mesh.meshletVertexIndexSRV.buffer->GetResource(), srvDesc);
		mesh.meshletVertexIndexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.meshletVertexIndexSRV.srvIndex);
		mesh.meshletVertexIndexSRV.buffer->SetSRVGPUHandle(mesh.meshletVertexIndexSRV.srvGPUHandle);

		mesh.usePackedMeshletVertexIndices = CanPackMeshletVertexIndices(imported.meshletVertexIndices);
		if (mesh.usePackedMeshletVertexIndices) {

			// 16bitに収まるメッシュレット頂点Indexは2個ずつ詰めて転送量を減らす
			std::vector<uint32_t> packedIndices = BuildPackedMeshletVertexIndices(imported.meshletVertexIndices);
			mesh.packedMeshletVertexIndexSRV.buffer = std::make_unique<DxStructuredBuffer<uint32_t>>();
			mesh.packedMeshletVertexIndexSRV.buffer->CreateSRVBuffer(device_, static_cast<UINT>(packedIndices.size()));
			mesh.packedMeshletVertexIndexSRV.buffer->TransferData(packedIndices);
			auto packedSrvDesc = mesh.packedMeshletVertexIndexSRV.buffer->GetSRVDesc(static_cast<UINT>(packedIndices.size()));
			srvDescriptor_->CreateSRV(mesh.packedMeshletVertexIndexSRV.srvIndex,
				mesh.packedMeshletVertexIndexSRV.buffer->GetResource(), packedSrvDesc);
			mesh.packedMeshletVertexIndexSRV.srvGPUHandle = srvDescriptor_->GetGPUHandle(mesh.packedMeshletVertexIndexSRV.srvIndex);
			mesh.packedMeshletVertexIndexSRV.buffer->SetSRVGPUHandle(mesh.packedMeshletVertexIndexSRV.srvGPUHandle);
		}
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
