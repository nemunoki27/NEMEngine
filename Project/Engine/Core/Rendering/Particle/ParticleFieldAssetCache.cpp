#include "ParticleFieldAssetCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>

// c++
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

using namespace Engine;

ParticleFieldAssetCache::ParticleFieldAssetCache() {

	workers_.Start(2, [](Request&& request, [[maybe_unused]] uint32_t workerIndex) { Decode(std::move(request)); });
}

ParticleFieldAssetCache::~ParticleFieldAssetCache() {

	workers_.Stop();
}

std::shared_ptr<ParticleFieldAssetCache::Result> ParticleFieldAssetCache::RequestAsset(
	const AssetDatabase& database, AssetID asset, bool heightmap) {

	if (!asset) return {};
	const auto lifetime = database.GetCacheLifetime();
	if (databaseLifetime_.owner_before(lifetime) || lifetime.owner_before(databaseLifetime_)) {
		vectors_.clear();
		heights_.clear();
		databaseLifetime_ = lifetime;
	}
	auto& entry = (heightmap ? heights_ : vectors_)[asset];
	const auto revision = database.GetContentRevision(asset);
	const auto structure = database.GetStructureRevision();
	if (!entry.result || entry.revision != revision || entry.structureRevision != structure) {
		if (entry.result && entry.result->ready.load(std::memory_order_acquire) && (entry.result->vector || entry.result->height))
			entry.retained = entry.result;
		entry.revision = revision;
		entry.structureRevision = structure;
		entry.result = std::make_shared<Result>();
		const auto path = database.ResolveFullPath(asset);
		auto extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		if (path.empty() || (!heightmap && extension != ".fga")) {
			entry.result->diagnostic = heightmap ? "Heightmapの参照先が見つかりません" : "FGAの参照先が見つかりません";
			entry.result->ready.store(true, std::memory_order_release);
		} else if (!workers_.Enqueue(Request{path, entry.result, heightmap})) {
			entry.result->diagnostic = "粒子入力の読込要求に失敗しました";
			entry.result->ready.store(true, std::memory_order_release);
		}
	}
	if ((!entry.result->ready.load(std::memory_order_acquire) || (!entry.result->vector && !entry.result->height)) && entry.retained)
		return entry.retained;
	return entry.result;
}

ParticleGPUWorldLoaders ParticleFieldAssetCache::MakeLoaders(const AssetDatabase& database) {

	ParticleGPUWorldLoaders loaders;
	loaders.vectorField = [this, &database](AssetID asset) {
		const auto result = RequestAsset(database, asset, false);
		return result && result->ready.load(std::memory_order_acquire) ? result->vector : nullptr;
	};
	loaders.heightmap = [this, &database](AssetID asset) {
		const auto result = RequestAsset(database, asset, true);
		return result && result->ready.load(std::memory_order_acquire) ? result->height : nullptr;
	};
	loaders.diagnostic = [this](AssetID asset) { return GetDiagnostic(asset); };
	return loaders;
}

std::string ParticleFieldAssetCache::GetDiagnostic(AssetID asset) const {

	for (const auto* entries : {&vectors_, &heights_}) {
		const auto found = entries->find(asset);
		if (found != entries->end() && found->second.result && found->second.result->ready.load(std::memory_order_acquire) &&
			!found->second.result->diagnostic.empty())
			return found->second.result->diagnostic;
	}
	return {};
}

void ParticleFieldAssetCache::Decode(Request request) {

	try {
		if (!request.heightmap) {
			std::ifstream stream(request.path);
			auto field = std::make_shared<VectorFieldData>();
			if (ReadFGA(stream, *field, request.result->diagnostic)) request.result->vector = std::move(field);
		} else {
			TextureFileRequestDesc description;
			description.assetPath = request.path.string();
			description.importSettings.preset = TextureImportPreset::Data;
			description.importSettings.generateMipmaps = false;
			description.importSettings.alphaColorBleed = false;
			description.requestedColorSpace = TextureColorSpace::Linear;
			description.overrideImportColorSpace = true;
			auto decoded = TextureDecoder::Decode(description);
			const auto* image = decoded.success ? decoded.image.GetImage(0, 0, 0) : nullptr;
			if (!image || image->width < 2 || image->height < 2 || image->width > 4096 || image->height > 4096)
				throw std::runtime_error("Heightmapの画像サイズは各軸2から4096です");
			DirectX::ScratchImage converted;
			if (image->format != DXGI_FORMAT_R32G32B32A32_FLOAT &&
				FAILED(DirectX::Convert(*image, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0, converted)))
				throw std::runtime_error("Heightmapの形式を変換できません");
			const auto* pixels = image->format == DXGI_FORMAT_R32G32B32A32_FLOAT ? image : converted.GetImage(0, 0, 0);
			auto data = std::make_shared<ParticleHeightmapData>();
			data->width = static_cast<uint32_t>(pixels->width);
			data->height = static_cast<uint32_t>(pixels->height);
			data->samples.resize(static_cast<size_t>(data->width) * data->height);
			for (uint32_t y = 0; y < data->height; ++y) {
				const auto* row = reinterpret_cast<const float*>(pixels->pixels + y * pixels->rowPitch);
				for (uint32_t x = 0; x < data->width; ++x) data->samples[x + y * data->width] = row[x * 4];
			}
			request.result->height = std::move(data);
		}
	} catch (const std::exception& error) {
		request.result->diagnostic = error.what();
	}
	request.result->ready.store(true, std::memory_order_release);
}
