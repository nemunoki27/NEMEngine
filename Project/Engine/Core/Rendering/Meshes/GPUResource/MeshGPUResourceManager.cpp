#include "MeshGPUResourceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <algorithm>
#include <cmath>
#include <span>

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

		// MeshShaderで読む法線・接線を圧縮した頂点配列を作る
		std::vector<Engine::MeshPackedVertex> packed{};
		packed.reserve(vertices.size());
		for (const Engine::MeshVertex& vertex : vertices) {

			Engine::MeshPackedVertex dst{};
			dst.normalOct = EncodeOctNormal(vertex.normal);
			dst.tangentOct = EncodeOctNormal(vertex.tangent);
			// 接線の利き手は圧縮せずそのまま保持する
			dst.tangentSign = vertex.tangentSign;
			dst.uv = vertex.uv;
			dst.position = vertex.position;
			packed.emplace_back(dst);
		}
		return packed;
	}

	template <typename T>
	void CreateImmutableSRV(ID3D12Device* device, Engine::BufferUploadService& uploadService,
		Engine::SRVDescriptor& srvDescriptor, Engine::MeshStructuredHandle<T>& out,
		const std::vector<T>& data, const wchar_t* debugName) {

		// 空データはWidth 0のD3D12 bufferを作れないため、SRV自体を未生成として扱う
		if (data.empty()) {
			return;
		}

		// 静的メッシュデータはDEFAULT heapへ置き、初期転送だけをUploadServiceへ集約する
		out.buffer = std::make_unique<Engine::DxImmutableStructuredBuffer<T>>();
		out.buffer->Create(device, uploadService, std::span<const T>(data.data(), data.size()));
		if (ID3D12Resource* resource = out.buffer->GetResource()) {
			resource->SetName(debugName);
		}
		// DescriptorはMeshStructuredHandle::Releaseで解放するため、handle側にindex/handleを保持する
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = out.buffer->GetSRVDesc();
		srvDescriptor.CreateSRV(out.srvIndex, out.buffer->GetResource(), srvDesc);
		out.srvGPUHandle = srvDescriptor.GetGPUHandle(out.srvIndex);
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
	uploadService_ = &graphicsCore.GetBufferUploadService();

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
	uploadService_ = nullptr;
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

	if (!uploadService_) {
		return;
	}

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
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.vertexSRV, imported.vertices, L"MeshVertices");
	}

	// 描画用圧縮頂点SRVリソース
	{
		// MeshShader側の帯域削減用に、法線を圧縮した頂点バッファも作る
		std::vector<MeshPackedVertex> packedVertices = BuildPackedVertices(imported.vertices);
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.packedVertexSRV, packedVertices, L"MeshPackedVertices");
	}

	// インデックスバッファ
	{
		// 16bitに収まるメッシュはIBVだけ16bit化して帯域を減らす
		const bool useIndex16 = CanPackMeshletVertexIndices(imported.indices);
		if (useIndex16) {
			const std::vector<uint16_t> indices16 = BuildIndex16(imported.indices);
			// BLAS構築でも同じIBを読むため、最終状態はINDEX_BUFFER単独ではなくGENERIC_READにする
			mesh.indexBuffer.Create(device_, *uploadService_, std::span(indices16),
				D3D12_RESOURCE_STATE_GENERIC_READ);
		} else {
			// SRV用indexSRVは32bitのまま別途保持し、IBVだけ描画向けに最適化する
			mesh.indexBuffer.Create(device_, *uploadService_, std::span(imported.indices),
				DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
	}

	// インデックスSRVリソース
	{
		// シェーダ側では32bit Indexとして読むため、SRVは従来どおり32bitを保持する
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.indexSRV, imported.indices, L"MeshIndices");
	}

	// スキニングインフルエンスSRVリソース
	if (mesh.isSkinned && !imported.vertexInfluences.empty()) {
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.skinInfluenceSRV, imported.vertexInfluences, L"SkinInfluences");
	}

	// 頂点サブメッシュインデックスSRVリソース
	{
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.vertexSubMeshIndexSRV, imported.vertexSubMeshIndices, L"MeshVertexSubMeshIndices");
	}

	// PrimitiveIndex()->サブメッシュインデックス参照用
	{
		std::vector<uint32_t> primitiveSubMeshTable = BuildPrimitiveSubMeshTable(mesh.subMeshes, mesh.indexCount);
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.primitiveSubMeshIndexSRV, primitiveSubMeshTable, L"MeshPrimitiveSubMeshIndices");
	}

	// メッシュレットSRVリソース
	if (!imported.meshlets.empty()) {
		// 互換用のフルDescも保持しておく
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.meshletSRV, imported.meshlets, L"Meshlets");

		// MSが使う範囲情報だけを分離して読み込み量を減らす
		std::vector<MeshletDrawDesc> drawDescs = BuildMeshletDrawDescs(imported.meshlets);
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.meshletDrawSRV, drawDescs, L"MeshletDrawDescs");

		// ASで先にカリングできるようBounds/NormalConeだけを分離する
		std::vector<MeshletBounds> bounds = BuildMeshletBounds(imported.meshlets);
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.meshletBoundsSRV, bounds, L"MeshletBounds");
	}
	if (!imported.meshletVertexIndices.empty()) {
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.meshletVertexIndexSRV, imported.meshletVertexIndices, L"MeshletVertexIndices");

		mesh.usePackedMeshletVertexIndices = CanPackMeshletVertexIndices(imported.meshletVertexIndices);
		if (mesh.usePackedMeshletVertexIndices) {

			// 16bitに収まるメッシュレット頂点Indexは2個ずつ詰めて転送量を減らす
			std::vector<uint32_t> packedIndices = BuildPackedMeshletVertexIndices(imported.meshletVertexIndices);
			CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
				mesh.packedMeshletVertexIndexSRV, packedIndices, L"PackedMeshletVertexIndices");
		}
	}
	if (!imported.meshletPrimitiveIndices.empty()) {
		CreateImmutableSRV(device_, *uploadService_, *srvDescriptor_,
			mesh.meshletPrimitiveIndexSRV, imported.meshletPrimitiveIndices, L"MeshletPrimitiveIndices");
	}

	// このメッシュで積んだDEFAULT heap初期転送を1Batchとして提出する。描画Queue側はGPU Waitで順序保証する
	uploadService_->SubmitBatch();

	// GPUリソースを保存
	{
		std::scoped_lock lock(mutex_);
		gpuMeshes_.emplace(imported.assetID, std::move(mesh));
	}
}
