#include "ProductAssetCook.h"

//============================================================================
//	include
//============================================================================
#include "ProductMesh.h"
#include <Engine/Core/Audio/AudioDecoder.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>

namespace {
	using namespace Engine;

	bool CookTexture(const std::filesystem::path& source, const AssetMeta& meta,
		std::vector<uint8_t>& bytes, std::string& error) {

		TextureFileRequestDesc request;
		request.assetPath = Algorithm::PathToUTF8(source);
		request.importSettings = ParseTextureImportSettings(meta.importerSettings);
		auto decoded = TextureDecoder::Decode(request);
		if (!decoded.success) {
			error = std::string(decoded.failureStage) + " HRESULT=" + std::to_string(decoded.result);
			return false;
		}
		DirectX::Blob dds;
		const HRESULT result = DirectX::SaveToDDSMemory(decoded.image.GetImages(), decoded.image.GetImageCount(),
			decoded.metadata, DirectX::DDS_FLAGS_FORCE_DX10_EXT, dds);
		if (FAILED(result)) {
			error = "DDSの生成に失敗しました HRESULT=" + std::to_string(result);
			return false;
		}
		const auto* first = static_cast<const uint8_t*>(dds.GetBufferPointer());
		bytes.assign(first, first + dds.GetBufferSize());
		return true;
	}

	bool CookAudio(const std::filesystem::path& source, std::vector<uint8_t>& bytes) {
		const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
		ScopedCleanup comCleanup([initialized]() noexcept { if (SUCCEEDED(initialized)) CoUninitialize(); });
		AudioDecoder decoder;
		decoder.Init();
		ScopedCleanup mediaCleanup([&]() noexcept { decoder.Finalize(); });
		const auto extension = Algorithm::ToLower(Algorithm::PathToUTF8(source.extension()));
		auto audio = extension == ".mp3" ? decoder.LoadMP3File(source) : decoder.LoadWaveFile(source);
		if (audio.formatBlob.empty() || audio.pcmBuffer.empty()) return false;
		bytes.clear();
		ProductBinary::Writer writer(bytes);
		uint32_t signature = 0x31445541; // AUD1
		ProductBinary::Scalars(writer, signature);
		ProductBinary::Array(writer, audio.formatBlob);
		ProductBinary::Array(writer, audio.pcmBuffer);
		return true;
	}
}

bool Engine::ProductAssetCook::Convert(const std::filesystem::path& source, const AssetMeta& meta,
	std::vector<uint8_t>& bytes, std::string& error) {

	try {
		switch (meta.type) {
		case AssetType::Texture: return CookTexture(source, meta, bytes, error);
		case AssetType::Mesh:
			if (ProductMesh::Cook(source, meta.guid, bytes)) return true;
			error = "メッシュの頂点・インデックスを取得できません";
			break;
		case AssetType::Audio:
			if (CookAudio(source, bytes)) return true;
			error = "音声を製品用PCMへ変換できません";
			break;
		default:
			error = "製品バイナリへの変換対象ではありません";
			break;
		}
	} catch (const std::exception& exception) {
		error = exception.what();
	}
	return false;
}
