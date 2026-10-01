#include "GPUMeshPublicationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportSettings.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <cstddef>
#include <fstream>
#include <vector>

bool NEMTests::RecordMeshPublication(ID3D12Device* device, ID3D12CommandQueue* queue,
	ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback) {

	using namespace Engine;
	TestDirectory directory("MeshPublication", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "reload.obj";
	const auto lodPath = directory.GetPath() / "reload_lod1.obj";
	auto save = [&](int height) {
		std::ofstream file(path);
		file << "o mesh" << height << "\nv 0 0 " << height << "\nv 1 0 " << height <<
			"\nv 0 1 " << height << "\nf 1 2 3\n";
		if (7 <= height) {
			file << "o extra\nv 2 0 " << height << "\nv 3 0 " << height <<
				"\nv 2 1 " << height << "\nf 4 5 6\n";
		}
		file.close();
		return !file.fail();
	};
	if (!save(3)) return false;
	{
		std::ofstream file(lodPath);
		file << "o mesh3\nv 0 0 3\nv 1 0 3\nv 0 1 3\nv 1 1 3\n"
			"f 1 2 3\nf 2 4 3\n";
		if (file.fail()) return false;
	}
	AssetDatabase database;
	database.Init();
	const AssetID asset = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Mesh);
	const AssetID lodAsset = database.ImportOrGet(
		RuntimePaths::ToAssetPath(lodPath), AssetType::Mesh);
	MeshImportSettings settings{};
	settings.manualLODMeshes[0] = lodAsset;
	if (!asset || !lodAsset || !database.UpdateImporterSettings(
		asset, ToJson(settings), kMeshImporterVersion)) return false;
	std::vector<MeshSubMeshLayoutItem> initialLayout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(&database, asset, initialLayout) || initialLayout.empty()) return false;
	BufferUploadService uploads;
	uploads.Init(descriptors.GetRetirementQueue(), device, queue);
	MeshGPUResourceManager meshes;
	meshes.Init(device, uploads, descriptors);

	// 初回要求に失敗しても同じAssetを再試行する
	std::filesystem::remove(path);
	meshes.RequestMesh(database, asset);
	meshes.WaitAll();
	if (meshes.Find(asset) || !save(3)) return false;
	meshes.RequestMesh(database, asset);
	// 初回読込中の変更も最後の内容で完了する
	if (!save(5)) return false;
	meshes.RequestReload(asset);
	meshes.RequestReload(asset);
	meshes.WaitAll();
	const auto* first = meshes.Find(asset);
	if (!first || !first->IsValid() || first->vertexCount != 7 ||
		first->lods[1].indexCount != 6) return false;
	std::vector<MeshSubMeshLayoutItem> publishedLayout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(&database, asset, publishedLayout) || publishedLayout.empty()) return false;
	const size_t publishedSubMeshCount = publishedLayout.size();
	const uint32_t firstIndex = first->vertexSRV.srvIndex;
	const uint32_t firstGeneration = first->reloadGeneration;
	const uint64_t firstRevision = meshes.GetResourceRevision();
	DxUtils::CreateReadbackBufferResource(device, readback, sizeof(float) * 2);
	// 旧Meshの使用を提出前の描画リストへ残す
	constexpr size_t positionZ = offsetof(MeshVertex, position) + sizeof(float) * 2;
	commands->CopyBufferRegion(readback.Get(), 0, first->vertexSRV.buffer->GetResource(), positionZ, sizeof(float));
	auto retained = [&]() {
		const auto* current = meshes.Find(asset);
		return current && current->IsValid() && current->vertexSRV.srvIndex == firstIndex &&
			current->reloadGeneration == firstGeneration && meshes.GetResourceRevision() == firstRevision;
	};

	// 壊れたファイルの読込失敗では旧表示を維持する
	{
		std::ofstream file(path);
		file << "invalid mesh";
	}
	meshes.RequestReload(asset);
	meshes.WaitAll();
	std::vector<MeshSubMeshLayoutItem> failedLayout;
	if (!retained() || !MeshSubMeshAuthoring::TryBuildLayout(&database, asset, failedLayout) ||
		failedLayout.size() != publishedSubMeshCount || !save(7)) return false;

	// Descriptor不足でGPU生成を失敗させる
	std::vector<uint32_t> occupied;
	occupied.reserve(descriptors.GetMaxSRVCount() - descriptors.GetUseDescriptorCount());
	while (descriptors.GetUseDescriptorCount() < descriptors.GetMaxSRVCount()) {
		occupied.push_back(descriptors.Allocate());
	}
	meshes.RequestReload(asset);
	meshes.WaitAll();
	const bool retainedOnFailure = retained();
	for (uint32_t index : occupied) {
		descriptors.Free(index);
	}
	if (!retainedOnFailure) return false;

	// 再試行に成功した世代だけ公開する
	meshes.RequestReload(asset);
	meshes.RequestReload(asset);
	meshes.WaitAll();
	const auto* second = meshes.Find(asset);
	if (!second || !second->IsValid() || second->vertexSRV.srvIndex == firstIndex ||
		second->reloadGeneration != firstGeneration + 1 || meshes.GetResourceRevision() != firstRevision + 1 ||
		!descriptors.IsAllocated(firstIndex)) return false;
	std::vector<MeshSubMeshLayoutItem> reloadedLayout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(&database, asset, reloadedLayout) ||
		reloadedLayout.size() == publishedSubMeshCount) return false;
	const uint32_t secondIndex = second->vertexSRV.srvIndex;
	commands->CopyBufferRegion(readback.Get(), sizeof(float), second->vertexSRV.buffer->GetResource(), positionZ, sizeof(float));
	meshes.Finalize();
	if (meshes.Find(asset) || meshes.GetResourceRevision() != firstRevision + 2) return false;
	// 再初期化しても同じAssetの旧世代を再利用しない
	meshes.Init(device, uploads, descriptors);
	meshes.RequestMesh(database, asset);
	meshes.WaitAll();
	const auto* reopened = meshes.Find(asset);
	if (!reopened || reopened->reloadGeneration != firstGeneration + 2) return false;
	meshes.Finalize();
	uploads.Finalize();
	return !meshes.Find(asset) && meshes.GetResourceRevision() == firstRevision + 4 &&
		descriptors.IsAllocated(firstIndex) && descriptors.IsAllocated(secondIndex);
}
