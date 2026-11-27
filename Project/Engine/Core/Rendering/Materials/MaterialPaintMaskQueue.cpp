#include "MaterialPaintMask.h"

//============================================================================
//	include
//============================================================================
#include "MaterialPaintAtlas.h"
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Watch/PublishedAssetChanges.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StoragePreparationDirectory.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/World/Components/Rendering/MaterialPaintComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

#include <deque>
#include <future>
#include <unordered_set>

namespace {

	using namespace Engine;

	struct MaskSnapshot {
		AssetMeta meta;
		std::filesystem::path path;
		std::filesystem::path staging;
		std::weak_ptr<const uint8_t> database;
		const AssetDatabase* owner = nullptr;
		std::shared_ptr<const MaterialPaintAtlas> atlas;
		std::vector<uint8_t> pixels;
		std::vector<uint8_t> preparedPixels;
		uint32_t resolution = 0;
		JsonFileJournal::Scope scope;
		std::future<std::string> written;
		bool failed = false;
	};

	struct MaskQueue {
		std::deque<std::shared_ptr<MaskSnapshot>> pending, recent;
		AssetWorkerPool<std::packaged_task<std::string()>> worker;
		std::weak_ptr<const uint8_t> database;
		std::filesystem::path directory;
		std::filesystem::path staging;
		JsonFileJournal::Scope scope;
		bool writable = false, started = false;
	};

	MaskQueue& GetQueue() {
		static MaskQueue queue;
		return queue;
	}

	std::string PublishFiles(const MaskSnapshot& snapshot, std::vector<JsonFileChange>& files) {
		using namespace StorageFileUtility;
		if (!snapshot.scope.isWritable(snapshot.path) || !snapshot.scope.isWritable(files[0].path))
			return "マスクの保存先に書き込めません";
		const auto final = snapshot.path.parent_path();
		std::filesystem::create_directories(final.parent_path());
		std::filesystem::create_directories(snapshot.staging);
		const auto stage = snapshot.staging / ToString(AssetID::New());
		if (!std::filesystem::create_directory(stage)) return "マスクの保存準備先を作成できません";
		StoragePreparationDirectory preparation(stage);
		const auto write = [&](const Path& path, const std::string& bytes) {
			preparation.Track(path, ContentHash::SHA256({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}));
			FileIdentity identity;
			if (!WriteBytesWithoutReplacement(path, bytes, identity)) return false;
			preparation.Capture(path, identity);
			return true;
		};
		if (!write(stage / files[0].path.filename(), JsonAdapter::SerializeCanonical(files[0].data)) ||
			!write(stage / files[1].path.filename(), *files[1].bytes)) return "マスクのファイルを作成できません";
		std::error_code ec;
		// PNGとmetaの両方が揃ってからGameAssetsに公開する
		if (MoveWithoutReplacement(stage, final, ec)) { preparation.Publish(); return {}; }
		if (ec != std::errc::cross_device_link) return ec.message();
		// SavedとGameAssetsが異なるドライブにある場合は通常の複数文書保存を使う
		std::string error;
		const auto recover = [&](const Path& directory, std::string& reason) {
			return JsonFileJournal::Recover(snapshot.scope, directory, reason, {});
		};
		if (!JsonFileJournal::Commit(snapshot.scope, files, "ペイントマスクの保存", error, recover))
			return error.empty() ? std::string("マスクの保存に失敗しました") : error;
		return {};
	}

	bool Schedule(MaskSnapshot& snapshot) {
		std::packaged_task<std::string()> job([&snapshot] {
			// 次のストロークは元画素を使い、保存画像だけUVの余白を埋める
			auto pixels = snapshot.pixels;
			DilateMaterialPaintMask(*snapshot.atlas, snapshot.resolution, pixels);
			std::string png;
			if (!EncodeMaterialPaintMask(snapshot.resolution, pixels, png)) return std::string("PNGの変換に失敗しました");
			snapshot.meta.importer = AssetMetaStorage::ResolveImporterName(AssetType::Texture);
			snapshot.meta.importerVersion = kTextureImporterVersion;
			auto settings = MakeTextureImportSettings(TextureImportPreset::Data);
			settings.addressU = settings.addressV = TextureAddressMode::Clamp;
			settings.alphaColorBleed = false;
			snapshot.meta.importerSettings = ToJson(settings);
			std::vector<JsonFileChange> files(2);
			files[0].path = AssetMetaStorage::MetaPathOf(snapshot.path);
			files[0].createOnly = files[1].createOnly = true;
			if (!AssetMetaStorage::BuildMetaDocument(snapshot.meta, nlohmann::json::object(), files[0].data))
				return std::string("マスクのメタデータを作成できません");
			files[1].path = snapshot.path;
			files[1].bytes.emplace(std::move(png));
			auto error = PublishFiles(snapshot, files);
			if (!error.empty()) return error;
			snapshot.preparedPixels = std::move(pixels);
			return std::string{};
		});
		snapshot.written = job.get_future();
		snapshot.failed = false;
		auto& queue = GetQueue();
		if (!queue.started) {
			queue.worker.Start(1, [](std::packaged_task<std::string()>&& task, uint32_t) { task(); });
			queue.started = true;
		}
		return queue.worker.Enqueue(std::move(job));
	}
}

Engine::AssetID Engine::QueueMaterialPaintMask(AssetDatabase& database, uint32_t resolution,
	std::vector<uint8_t>&& rgba, std::shared_ptr<const MaterialPaintAtlas> atlas) {

	if (!atlas || (resolution != 512 && resolution != 1024 && resolution != 2048) ||
		rgba.size() != size_t(resolution) * resolution * 4) return {};
	auto item = std::make_shared<MaskSnapshot>();
	item->meta.guid = AssetID::New();
	item->meta.type = AssetType::Texture;
	auto& queue = GetQueue();
	const auto life = database.GetCacheLifetime().lock();
	if (queue.database.lock() != life) {
		queue.database = life;
		queue.directory = database.ResolveAssetPath("GameAssets/PaintData/Masks");
		queue.staging = RuntimePaths::GetSavedRoot() / "MaterialPaintMaskStaging";
		queue.scope = AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::GeneratedRender);
		queue.writable = !queue.directory.empty() && queue.scope.isWritable(queue.directory / "mask.png");
	}
	if (!queue.writable) return {};
	const auto folder = ToString(item->meta.guid);
	item->meta.assetPath = "GameAssets/PaintData/Masks/" + folder + "/mask.png";
	item->path = queue.directory / folder / "mask.png";
	item->staging = queue.staging;
	item->scope = queue.scope;
	item->resolution = resolution;
	item->database = database.GetCacheLifetime();
	item->owner = &database;
	item->pixels = std::move(rgba);
	item->atlas = std::move(atlas);
	queue.pending.push_back(item);
	if (!Schedule(*item)) { queue.pending.pop_back(); return {}; }
	return item->meta.guid;
}

bool Engine::FindMaterialPaintMaskPixels(const AssetDatabase& database, AssetID mask, uint32_t resolution,
	std::vector<uint8_t>& rgba, bool forStroke) {

	const auto life = database.GetCacheLifetime().lock();
	const auto& queue = GetQueue();
	for (const auto* list : {&queue.pending, &queue.recent}) {
		for (const auto& item : *list) {
			if (item->meta.guid != mask || item->resolution != resolution || item->owner != &database) continue;
			if (item->database.lock() != life && item->path != database.ResolveAssetPath(item->meta.assetPath)) continue;
			if (forStroke && list == &queue.recent && !item->preparedPixels.empty()) rgba = std::move(item->preparedPixels);
			else rgba = item->pixels;
			return true;
		}
	}
	return false;
}

namespace {

bool PublishMasks(AssetDatabase& database, bool wait, const std::unordered_set<AssetID>* required) {

	auto& queue = GetQueue();
	if (queue.pending.empty()) return true;
	const auto life = database.GetCacheLifetime().lock();
	bool ok = true;
	for (auto it = queue.pending.begin(); it != queue.pending.end();) {
		auto& item = **it;
		if (item.database.lock() != life && item.path != database.ResolveAssetPath(item.meta.assetPath)) {
			// 破棄したプロジェクトの書き込みもワーカー完了まで保持する
			if (item.database.expired() && (!item.written.valid() ||
				item.written.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) it = queue.pending.erase(it);
			else ++it;
			continue;
		}
		item.database = life;
		item.owner = &database;
		if (required && !required->contains(item.meta.guid)) { ++it; continue; }
		if (item.failed && (!wait || !Schedule(item))) { ++it; ok = false; continue; }
		if (!wait && item.written.valid() && item.written.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
			++it;
			continue;
		}
		std::string error;
		try { if (item.written.valid()) error = item.written.get(); }
		catch (const std::exception& e) { error = e.what(); }
		if (!error.empty()) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[MaterialPaint] {} {}", item.meta.assetPath, error);
			item.failed = true;
			ok = false;
			++it;
			continue;
		}
		if (PublishedAssetChanges::Import(database, item.meta.assetPath, AssetType::Texture) != item.meta.guid) {
			ok = false;
			++it;
			continue;
		}
		item.atlas.reset();
		// 保存用の画素を正本とし、UV余白のない作業領域は次の塗布に引き継ぐ
		item.pixels.swap(item.preparedPixels);
		queue.recent.emplace_back(std::move(*it));
		it = queue.pending.erase(it);
		while (queue.recent.size() > 4) queue.recent.pop_front();
	}
	return ok;
}
}

bool Engine::PublishMaterialPaintMasks(AssetDatabase& database, bool wait) {
	return PublishMasks(database, wait, nullptr);
}

bool Engine::PublishMaterialPaintMasks(AssetDatabase& database, const ECSWorld& world, std::span<const Entity> objects) {
	if (GetQueue().pending.empty()) return true;
	std::unordered_set<AssetID> masks;
	for (const auto object : objects) {
		if (const auto* paint = world.TryGetComponent<MaterialPaintComponent>(object)) masks.emplace(paint->mask);
	}
	return PublishMasks(database, true, &masks);
}

bool Engine::MaterialPaintSaveQueueFull() {
	constexpr size_t maxQueuedBytes = 128 * 1024 * 1024;
	size_t bytes = 0;
	for (const auto& item : GetQueue().pending) {
		if (!item->failed) bytes += item->pixels.size();
	}
	return maxQueuedBytes <= bytes;
}
