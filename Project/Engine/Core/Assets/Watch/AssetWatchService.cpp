#include "AssetWatchService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Watch/AssetChangeWatcher.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	AssetWatchService classMethods
//============================================================================
namespace {

	// 検知は毎フレームではなくこの間隔のフレームごとに行う
	constexpr uint32_t kPollIntervalFrames = 20;
	// 変更検知から実際のリロードまで待つ時間で、エディタの書き込み途中を読まないための猶予
	constexpr std::chrono::milliseconds kDebounceDuration{ 300 };

	// 対象とするテクスチャ拡張子か
	bool IsTextureExtension(const std::string& extension) {

		return extension == ".png" || extension == ".dds" || extension == ".tga" ||
			extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".gif";
	}

	// 対象とするモデル拡張子か
	bool IsModelExtension(const std::string& extension) {

		return extension == ".obj" || extension == ".gltf" || extension == ".glb" ||
			extension == ".fbx" || extension == ".mtl";
	}
}

Engine::AssetWatchService::AssetWatchService() = default;

Engine::AssetWatchService::~AssetWatchService() {
	Stop();
}

void Engine::AssetWatchService::Start(AssetDatabase* assetDatabase, TextureUploadService* textureUploadService,
	const std::vector<std::filesystem::path>& roots) {

	Stop();

	assetDatabase_ = assetDatabase;
	textureUploadService_ = textureUploadService;

	// 監視ルートごとにwatcherを張る、存在しないルートはスキップする
	for (const std::filesystem::path& root : roots) {

		auto watcher = std::make_unique<AssetChangeWatcher>();
		if (watcher->Start(root)) {

			watchers_.emplace_back(std::move(watcher));
			Logger::Output(LogType::Engine, "[AssetWatch] watching: {}", root.generic_string());
		}
	}
}

void Engine::AssetWatchService::Stop() {

	for (std::unique_ptr<AssetChangeWatcher>& watcher : watchers_) {
		watcher->Stop();
	}
	watchers_.clear();
	pendingChanges_.clear();
	assetDatabase_ = nullptr;
	textureUploadService_ = nullptr;
}

void Engine::AssetWatchService::Update() {

	if (watchers_.empty()) {
		return;
	}

	// 数フレームに一度だけ検知処理を行いmainループへの負荷を抑える
	if (++frameCounter_ < kPollIntervalFrames) {
		return;
	}
	frameCounter_ = 0;

	// 背景スレッドが溜めた変更パスを回収する
	std::vector<std::filesystem::path> changed;
	for (std::unique_ptr<AssetChangeWatcher>& watcher : watchers_) {
		watcher->DrainChanges(changed);
	}

	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	// 変更を受理時刻付きで控える、連続書き込みは最後の時刻で上書きしてdebounceを延ばす
	for (const std::filesystem::path& path : changed) {

		std::error_code ec{};
		// ディレクトリ変更や削除は対象外、実ファイルだけを扱う
		if (!std::filesystem::is_regular_file(path, ec) || ec) {
			continue;
		}
		pendingChanges_[path.generic_string()] = now;
	}

	// debounce窓を過ぎて安定した変更だけをリロードへ回す
	for (auto it = pendingChanges_.begin(); it != pendingChanges_.end();) {

		if (now - it->second >= kDebounceDuration) {

			DispatchReload(std::filesystem::path(it->first));
			it = pendingChanges_.erase(it);
		} else {
			++it;
		}
	}
}

void Engine::AssetWatchService::DispatchReload(const std::filesystem::path& path) {

	const std::string extension = Algorithm::ToLower(path.extension().string());

	if (!assetDatabase_) {
		return;
	}

	// .mtlはアセットそのものではないので、同じstemの.objを探してそのモデルを再ロードする
	if (extension == ".mtl") {

		if (!meshReloadCallback_) {
			return;
		}
		std::filesystem::path objPath = path;
		objPath.replace_extension(".obj");
		const std::string objAssetPath = RuntimePaths::ToAssetPath(objPath.string());
		if (const AssetMeta* objMeta = objAssetPath.empty() ? nullptr : assetDatabase_->FindByPath(objAssetPath)) {

			meshReloadCallback_(objMeta->guid);
			Logger::Output(LogType::Engine, "[AssetWatch] mtl changed, reload requested for model: {}", objAssetPath);
		}
		return;
	}

	const bool isTexture = IsTextureExtension(extension);
	const bool isModel = IsModelExtension(extension);
	if (!isTexture && !isModel) {
		return;
	}

	// 監視ルート外のパスはアセットパスへ変換できないので無視する
	const std::string assetPath = RuntimePaths::ToAssetPath(path.string());
	if (assetPath.empty()) {
		return;
	}

	// AssetDatabaseに登録済みのアセットだけを対象にする
	const AssetMeta* meta = assetDatabase_->FindByPath(assetPath);
	if (!meta) {
		return;
	}

	if (isTexture && textureUploadService_) {

		// このファイルを指す全キー(描画用base/sRGBやProjectPanelサムネイル)をまとめて差し替える
		textureUploadService_->RequestReloadByFile(path);
		Logger::Output(LogType::Engine, "[AssetWatch] texture changed, reload requested: {}", assetPath);
	} else if (isModel && meshReloadCallback_) {

		// モデルはbackend側のmesh管理へAssetIDで委譲する
		meshReloadCallback_(meta->guid);
		Logger::Output(LogType::Engine, "[AssetWatch] model changed, reload requested: {}", assetPath);
	}
}
