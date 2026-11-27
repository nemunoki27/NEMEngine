#include "MaterialPaintMask.h"

#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>

#include <wincodec.h>
#include <wrl/client.h>
#include <future>
#include <map>

namespace {

	using Microsoft::WRL::ComPtr;

	bool ValidResolution(uint32_t resolution) {

		return resolution == 512 || resolution == 1024 || resolution == 2048;
	}

	bool EncodeMask(IWICImagingFactory& factory, uint32_t size, const std::vector<uint8_t>& rgba, std::string& bytes) {

		ComPtr<IStream> stream;
		ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame;
		if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
			FAILED(factory.CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
			FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
			FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
			FAILED(frame->SetSize(size, size))) return false;

		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		ComPtr<IWICBitmap> source;
		ComPtr<IWICFormatConverter> converted;
		if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA ||
			FAILED(factory.CreateBitmapFromMemory(size, size, GUID_WICPixelFormat32bppRGBA, size * 4,
				static_cast<UINT>(rgba.size()), const_cast<BYTE*>(rgba.data()), &source)) ||
			FAILED(factory.CreateFormatConverter(&converted)) ||
			FAILED(converted->Initialize(source.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
			FAILED(frame->WriteSource(converted.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;

		STATSTG info{};
		if (FAILED(stream->Stat(&info, STATFLAG_NONAME)) || info.cbSize.HighPart || !info.cbSize.LowPart ||
			FAILED(stream->Seek({}, STREAM_SEEK_SET, nullptr))) return false;
		bytes.resize(info.cbSize.LowPart);
		ULONG count = 0;
		return SUCCEEDED(stream->Read(bytes.data(), info.cbSize.LowPart, &count)) && count == bytes.size();
	}
}

namespace {

bool ReadMask(const std::filesystem::path& path, uint32_t resolution, std::vector<uint8_t>& rgba) {
	using namespace Engine;
	const size_t pitch = size_t(resolution) * 4;
	const auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
	ScopedCleanup cleanup([init]() noexcept { if (SUCCEEDED(init)) CoUninitialize(); });
	ComPtr<IWICImagingFactory> factory;
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter> source;
	UINT width = 0, height = 0;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
		FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame)) ||
		FAILED(frame->GetSize(&width, &height)) || width != resolution || height != resolution ||
		FAILED(factory->CreateFormatConverter(&source)) || FAILED(source->Initialize(frame.Get(),
			GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
	rgba.resize(pitch * resolution);
	return SUCCEEDED(source->CopyPixels(nullptr, static_cast<UINT>(pitch), static_cast<UINT>(rgba.size()), rgba.data()));
}
}

bool Engine::LoadMaterialPaintMask(const AssetDatabase& database, AssetID mask, uint32_t resolution,
	std::vector<uint8_t>& rgba, bool* preparing) {

	if (preparing) *preparing = false;
	if (!ValidResolution(resolution)) return false;
	if (!mask && !preparing) {
		rgba.assign(size_t(resolution) * resolution * 4, 0);
		return true;
	}
	if (mask && FindMaterialPaintMaskPixels(database, mask, resolution, rgba, preparing != nullptr)) return true;
	const auto path = mask ? database.ResolveFullPath(mask) : std::filesystem::path{};
	if (mask && path.empty()) return false;
	if (!preparing) return ReadMask(path, resolution, rgba);
	// 新規マスクのゼロ初期化と保存済みPNGのデコードを入力処理から分離する
	using Pixels = std::vector<uint8_t>;
	struct ReadRequests {
		std::map<std::pair<std::filesystem::path, uint32_t>, std::future<Pixels>> jobs;
		AssetWorkerPool<std::packaged_task<Pixels()>> worker;
		ReadRequests() { worker.Start(1, [](std::packaged_task<Pixels()>&& job, uint32_t) { job(); }); }
	};
	static ReadRequests requests;
	auto [it, added] = requests.jobs.try_emplace(std::make_pair(path, resolution));
	if (added) {
		std::packaged_task<Pixels()> job([path, resolution] {
			Pixels result;
			if (path.empty()) result.resize(size_t(resolution) * resolution * 4);
			else if (!ReadMask(path, resolution, result)) result.clear();
			return result;
		});
		it->second = job.get_future();
		requests.worker.Enqueue(std::move(job));
	}
	if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
		*preparing = true;
		return false;
	}
	try { rgba = it->second.get(); } catch (const std::exception&) { rgba.clear(); }
	requests.jobs.erase(it);
	return !rgba.empty();
}

bool Engine::EncodeMaterialPaintMask(uint32_t resolution, const std::vector<uint8_t>& rgba, std::string& png) {

	const auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
	ScopedCleanup cleanup([init]() noexcept { if (SUCCEEDED(init)) CoUninitialize(); });
	ComPtr<IWICImagingFactory> factory;
	return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		EncodeMask(*factory.Get(), resolution, rgba, png);
}

Engine::AssetID Engine::SaveMaterialPaintMask(AssetDatabase& database, uint32_t resolution, const std::vector<uint8_t>& rgba) {

	const size_t pitch = size_t(resolution) * 4;
	if (!ValidResolution(resolution) || rgba.size() != pitch * resolution) return {};
	const auto hash = ContentHash::SHA256(rgba);
	if (hash.empty()) return {};
	const std::string path = "GameAssets/PaintData/Masks/" + hash + ".png";
	if (const auto* meta = database.FindByPath(path)) return meta->type == AssetType::Texture ? meta->guid : AssetID{};
	std::string png;
	if (!EncodeMaterialPaintMask(resolution, rgba, png)) return {};
	AssetDocumentChange change;
	std::string diagnostic;
	if (!AssetDocumentPublication::Prepare(database, path, AssetType::Texture, change, diagnostic)) return {};
	auto settings = MakeTextureImportSettings(TextureImportPreset::Data);
	settings.addressU = settings.addressV = TextureAddressMode::Clamp;
	settings.alphaColorBleed = false;
	change.metadata.importerSettings = ToJson(settings);
	change.metadata.importerVersion = kTextureImporterVersion;
	change.metaDocument["settings"] = change.metadata.importerSettings;
	change.metaDocument["importerVersion"] = kTextureImporterVersion;
	change.bytes.emplace(std::move(png));
	const auto scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::GeneratedRender);
	return AssetDocumentPublication::Commit(database, {&change, 1}, scope, diagnostic) ? change.metadata.guid : AssetID{};
}
