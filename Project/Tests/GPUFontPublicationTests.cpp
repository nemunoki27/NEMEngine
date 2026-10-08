#include "GPUFontPublicationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/FontRenderService.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <cstring>

namespace {

	// 指定色のAtlas画像を作る
	bool SaveAtlas(const std::filesystem::path& path, uint32_t size, bool blue) {

		DirectX::ScratchImage image;
		if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1))) {
			return false;
		}
		for (size_t offset = 0; offset < image.GetPixelsSize(); offset += 4) {
			auto* pixel = image.GetPixels() + offset;
			pixel[0] = blue ? 0 : 255;
			pixel[1] = 0;
			pixel[2] = blue ? 255 : 0;
			pixel[3] = 255;
		}
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
			return false;
		}
		Engine::ScopedCleanup cleanup([initialized]() noexcept {
			if (SUCCEEDED(initialized)) {
				CoUninitialize();
			}
		});
		return SUCCEEDED(DirectX::SaveToWICFile(
			*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str()));
	}

	// GPUへ転送した画像の先頭色を読み戻す
	bool CheckPixel(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture, bool blue) {

		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commands;
		ComPtr<ID3D12Fence> fence;
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
			FAILED(device->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands))) ||
			FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
			return false;
		}
		const auto description = texture->GetDesc();
		const bool rgba = description.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
			description.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		const bool bgra = description.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
			description.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		if (!rgba && !bgra) {
			return false;
		}
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		uint64_t bytes = 0;
		device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
		ComPtr<ID3D12Resource> readback;
		DxUtils::CreateReadbackBufferResource(device, readback, bytes);
		D3D12_TEXTURE_COPY_LOCATION target{};
		target.pResource = readback.Get();
		target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		target.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = texture;
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
		if (FAILED(commands->Close())) {
			return false;
		}
		ID3D12CommandList* lists[]{commands.Get()};
		queue->ExecuteCommandLists(1, lists);
		HANDLE completed = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!completed) {
			return false;
		}
		Engine::ScopedCleanup cleanup([completed]() noexcept { CloseHandle(completed); });
		if (FAILED(queue->Signal(fence.Get(), 1)) || FAILED(fence->SetEventOnCompletion(1, completed)) ||
			WaitForSingleObject(completed, 10000) != WAIT_OBJECT_0) {
			return false;
		}
		void* mapped = nullptr;
		const D3D12_RANGE range{0, 4};
		if (FAILED(readback->Map(0, &range, &mapped))) {
			return false;
		}
		// WICの出力形式に合わせて赤と青の配置を比較する
		const bool firstBlue = bgra ? !blue : blue;
		const std::array<uint8_t, 4> expected{
			static_cast<uint8_t>(firstBlue ? 0 : 255), 0, static_cast<uint8_t>(firstBlue ? 255 : 0), 255};
		const bool matches = std::memcmp(mapped, expected.data(), expected.size()) == 0;
		const D3D12_RANGE written{0, 0};
		readback->Unmap(0, &written);
		return matches;
	}
} // namespace

bool NEMTests::CheckFontRenderPublication(ID3D12Device* device, ID3D12CommandQueue* queue) {

	using namespace Engine;
	TestDirectory directory("FontRenderPublication", RuntimePaths::GetGameAssetsRoot());
	const auto atlasPath = directory.GetPath() / "atlas.png";
	const auto fontPath = directory.GetPath() / "Fixture.font.json";
	AssetDatabase database;
	if (!database.Init() || !SaveAtlas(atlasPath, 2, false)) {
		return false;
	}
	const auto atlasID = database.ImportOrGet(RuntimePaths::ToAssetPath(atlasPath), AssetType::Texture);
	nlohmann::json document{{"name", "First"}, {"atlasTexture", ToString(atlasID)},
		{"atlas", {{"width", 2}, {"height", 2}, {"distanceRange", 8}}}, {"metrics", {{"emSize", 48}, {"lineHeight", 1.2}}},
		{"glyphs", nlohmann::json::array({{{"unicode", 65}, {"advance", 1}}})}};
	if (!atlasID || !JsonFile::SaveCanonical(fontPath, document, 2)) {
		return false;
	}
	const auto fontID = database.ImportOrGet(RuntimePaths::ToAssetPath(fontPath), AssetType::Font);
	if (!fontID) {
		return false;
	}
	GraphicsResourceRetirement retirement;
	SRVDescriptor descriptors;
	descriptors.Init(device, {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE});
	descriptors.SetRetirementQueue(retirement);
	TextureUploadService textures;
	textures.Init(device, &descriptors);
	FontRenderService fonts(textures);
	RenderAssetLibrary library;
	library.Init(&database);
	// 要求後のファイル変更を転送へ混ぜない
	if (fonts.Resolve(library, fontID).font || !SaveAtlas(atlasPath, 2, true)) {
		return false;
	}
	textures.WaitAll();
	const auto first = fonts.Resolve(library, fontID);
	if (!first.font || !first.atlas || !CheckPixel(device, queue, first.atlas->resource.Get(), false)) {
		return false;
	}
	auto firstRevision = first.font->contentRevision;
	auto firstIndex = first.atlas->srvIndex;
	TextureImportSettings settings;
	settings.generateMipmaps = false;
	if (!database.UpdateImporterSettings(atlasID, ToJson(settings), kTextureImporterVersion)) {
		return false;
	}
	// 通知だけでは公開済みの固定画像を変更しない
	textures.RequestReloadByFile(atlasPath, &settings);
	textures.WaitAll();
	if (!CheckPixel(device, queue, first.atlas->resource.Get(), false) ||
		fonts.Resolve(library, fontID).atlas->srvIndex != firstIndex) {
		return false;
	}
	// Atlas単体の更新も転送成功後に一組で公開する
	textures.WaitAll();
	const auto reloaded = fonts.Resolve(library, fontID);
	if (!reloaded.font || !reloaded.atlas || reloaded.font->contentRevision == firstRevision ||
		reloaded.atlas->srvIndex == firstIndex || !descriptors.IsAllocated(firstIndex) ||
		!CheckPixel(device, queue, reloaded.atlas->resource.Get(), true)) {
		return false;
	}
	if (reloaded.atlas->resource->GetDesc().MipLevels != 1) {
		return false;
	}
	firstRevision = reloaded.font->contentRevision;
	firstIndex = reloaded.atlas->srvIndex;
	// 無効な配置情報では旧CPUとGPU世代を保持する
	if (!StorageFileUtility::WriteBytes(fontPath, "invalid")) {
		return false;
	}
	library.InvalidateFont(fontID);
	if (fonts.Resolve(library, fontID).font->contentRevision != firstRevision) {
		return false;
	}
	// 無効画像とサイズ不一致でも旧表示を維持する
	for (bool mismatch : {false, true}) {
		document["name"] = mismatch ? "Mismatch" : "Failed";
		if (!JsonFile::SaveCanonical(fontPath, document, 2) ||
			!(mismatch ? SaveAtlas(atlasPath, 4, true) : StorageFileUtility::WriteBytes(atlasPath, "invalid"))) {
			return false;
		}
		library.InvalidateFont(fontID);
		fonts.Resolve(library, fontID);
		textures.WaitAll();
		const auto retained = fonts.Resolve(library, fontID);
		if (!retained.font || !retained.atlas || retained.font->contentRevision != firstRevision ||
			retained.atlas->srvIndex != firstIndex) {
			return false;
		}
	}
	// 再要求の成功後に一組を差し替え、旧Descriptorを保持する
	document["name"] = "Second";
	if (!SaveAtlas(atlasPath, 2, true) || !JsonFile::SaveCanonical(fontPath, document, 2)) {
		return false;
	}
	library.InvalidateFont(fontID);
	if (fonts.Resolve(library, fontID).font->contentRevision != firstRevision) {
		return false;
	}
	textures.WaitAll();
	const auto second = fonts.Resolve(library, fontID);
	if (!second.font || !second.atlas || second.font->name != "Second" || second.atlas->srvIndex == firstIndex ||
		!descriptors.IsAllocated(firstIndex) || !CheckPixel(device, queue, second.atlas->resource.Get(), true)) {
		return false;
	}
	const auto secondIndex = second.atlas->srvIndex;
	const auto secondRevision = second.font->contentRevision;
	if (fonts.Resolve(library, fontID).font->contentRevision != secondRevision) {
		return false;
	}
	// 別Libraryの同じAssetと内容番号を混同しない
	uint32_t otherIndex = UINT32_MAX;
	{
		RenderAssetLibrary otherLibrary;
		otherLibrary.Init(&database);
		if (fonts.Resolve(otherLibrary, fontID).font) {
			return false;
		}
		textures.WaitAll();
		const auto other = fonts.Resolve(otherLibrary, fontID);
		if (!other.font || !other.atlas || other.font->contentRevision == secondRevision ||
			other.atlas->srvIndex == secondIndex || !CheckPixel(device, queue, other.atlas->resource.Get(), true)) {
			return false;
		}
		otherIndex = other.atlas->srvIndex;
	}
	fonts.CollectExpired();
	if (!descriptors.IsAllocated(otherIndex) || fonts.Resolve(library, fontID).atlas->srvIndex != secondIndex) {
		return false;
	}
	// 転送中の取消はDescriptorを新たに公開しない
	const auto snapshot = library.LoadFontSource(fontID);
	const auto cancelled = textures.RequestSnapshot(snapshot->atlas);
	const auto count = descriptors.GetUseDescriptorCount();
	textures.ReleaseSnapshot(cancelled);
	textures.WaitAll();
	if (textures.GetState(cancelled) != TextureRequestState::None || descriptors.GetUseDescriptorCount() != count) {
		return false;
	}
	// LibraryのClear後は固定世代を回収窓口へ渡す
	library.Clear();
	fonts.CollectExpired();
	if (!descriptors.IsAllocated(secondIndex)) {
		return false;
	}
	retirement.Seal(1);
	retirement.Collect(1);
	const bool released = descriptors.GetUseDescriptorCount() == 0;
	fonts.Clear();
	textures.Finalize();
	return released;
}
