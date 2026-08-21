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
			extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
			extension == ".gif" || extension == ".hdr";
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
			Logger::Output(LogType::Engine, "[AssetWatch] watching: {}",
				Algorithm::PathToUTF8(root));
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
	// 削除やディレクトリの変更も構造変更として扱うため、存在チェックでは弾かずに全て控える
	for (const std::filesystem::path& path : changed) {
		pendingChanges_[path] = now;
	}

	// debounce窓を過ぎて安定した変更だけを処理へ回す
	// 内容リロードできなかった変更(追加/削除/リネーム等)はアセット集合の構造変更とみなす
	bool structureChanged = false;
	for (auto it = pendingChanges_.begin(); it != pendingChanges_.end();) {

		if (now - it->second >= kDebounceDuration) {

			const std::filesystem::path& path = it->first;
			if (!DispatchReload(path)) {

				// .metaはRebuildMetaが自前で発番する管理ファイルなので、再構築ループ回避のため無視する
				if (Algorithm::ToLower(
					Algorithm::PathToUTF8(path.extension())) != ".meta") {
					structureChanged = true;
				}
			}
			it = pendingChanges_.erase(it);
		} else {
			++it;
		}
	}

	// 構造変更があればAssetDatabaseを作り直し、構造リビジョンを進めてProjectPanel等へ知らせる
	if (structureChanged && assetDatabase_) {
		assetDatabase_->RebuildMeta();
	}
}

bool Engine::AssetWatchService::DispatchReload(const std::filesystem::path& path) {

	if (!assetDatabase_) {
		return false;
	}

	// 削除されたパスやディレクトリは内容リロードの対象にできない、構造変更として扱わせる
	std::error_code ec{};
	const bool isRegularFile = std::filesystem::is_regular_file(path, ec) && !ec;

	const std::string extension = Algorithm::ToLower(
		Algorithm::PathToUTF8(path.extension()));
	if (extension == ".meta") {

		// Importer設定の外部変更をDBと既存GPUテクスチャへ同時に反映する
		std::filesystem::path assetFullPath = path;
		assetFullPath.replace_extension();
		assetDatabase_->RebuildMeta();
		if (textureUploadService_) {

			const std::string changedAssetPath = RuntimePaths::ToAssetPath(assetFullPath);
			const AssetMeta* changedMeta = changedAssetPath.empty() ? nullptr :
				assetDatabase_->FindByPath(changedAssetPath);
			if (changedMeta && changedMeta->type == AssetType::Texture) {

				const TextureImportSettings settings = ParseTextureImportSettings(
					changedMeta->importerSettings);
				textureUploadService_->RequestReloadByFile(assetFullPath, &settings);
			}
		}
		Logger::Output(LogType::Engine,
			"[AssetWatch] importer settings changed: {}",
			Algorithm::PathToUTF8(assetFullPath));
		return true;
	}

	// 内容リロードは実ファイルが存在するときだけ行う、削除やディレクトリ変更は構造変更へ回す
	if (!isRegularFile) {
		return false;
	}

	// .mtlはアセットそのものではないので、同じstemの.objを探してそのモデルを再ロードする
	if (extension == ".mtl") {

		if (!meshReloadCallback_) {
			return false;
		}
		std::filesystem::path objPath = path;
		objPath.replace_extension(".obj");
		const std::string objAssetPath = RuntimePaths::ToAssetPath(objPath);
		if (const AssetMeta* objMeta = objAssetPath.empty() ? nullptr : assetDatabase_->FindByPath(objAssetPath)) {

			meshReloadCallback_(objMeta->guid);
			Logger::Output(LogType::Engine, "[AssetWatch] mtl changed, reload requested for model: {}", objAssetPath);
		}
		return true;
	}

	// 監視ルート外のパスはアセットパスへ変換できないので無視する
	const std::string assetPath = RuntimePaths::ToAssetPath(path);
	if (assetPath.empty()) {
		return false;
	}

	// AssetDatabaseに未登録なら新規追加とみなし、内容リロードではなく構造変更として扱わせる
	const AssetMeta* meta = assetDatabase_->FindByPath(assetPath);
	if (!meta) {
		return false;
	}

	const bool isTexture = IsTextureExtension(extension);
	const bool isModel = IsModelExtension(extension);
	if (meta->type == AssetType::Texture && isTexture && textureUploadService_) {

		// このファイルを指す全キー(描画用base/sRGBやProjectPanelサムネイル)をまとめて差し替える
		textureUploadService_->RequestReloadByFile(path);
		Logger::Output(LogType::Engine, "[AssetWatch] texture changed, reload requested: {}", assetPath);
	} else if (meta->type == AssetType::Mesh && isModel && meshReloadCallback_) {

		// モデルはbackend側のmesh管理へAssetIDで委譲する
		meshReloadCallback_(meta->guid);
		Logger::Output(LogType::Engine, "[AssetWatch] model changed, reload requested: {}", assetPath);
	} else if ((meta->type == AssetType::Material || meta->type == AssetType::Shader ||
		meta->type == AssetType::RenderPipeline) && renderAssetReloadCallback_) {

		// 描画アセットは依存関係を含めてRenderPipelineRunner側で再ロードする
		renderAssetReloadCallback_(meta->guid);
		Logger::Output(LogType::Engine, "[AssetWatch] render asset changed, reload requested: {}", assetPath);
	} else {
		return false;
	}
	return true;
}
