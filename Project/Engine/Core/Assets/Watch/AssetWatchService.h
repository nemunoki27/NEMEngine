#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	// front
	class AssetDatabase;
	class TextureUploadService;
	class AssetChangeWatcher;

	//============================================================================
	//	AssetWatchService class
	//	アセットの外部編集を非同期監視し、数フレームに一度まとめてホットリロードを発火するサービス
	//	監視は専用スレッド任せでmainループに影響を出さず、リロードのGPU処理はmainスレッドで行う
	//============================================================================
	class AssetWatchService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AssetWatchService();
		~AssetWatchService();

		AssetWatchService(const AssetWatchService&) = delete;
		AssetWatchService& operator=(const AssetWatchService&) = delete;

		// 監視ルートを登録して監視を開始する、textureのリロードにはAssetDatabaseとTextureUploadServiceを使う
		void Start(AssetDatabase* assetDatabase, TextureUploadService* textureUploadService,
			const std::vector<std::filesystem::path>& roots);
		// 監視を停止する、複数回呼んでも安全
		void Stop();

		// モデル変更時のリロード処理を外部から差し込む、対象メッシュはAssetIDで指す
		void SetMeshReloadCallback(std::function<void(AssetID)> callback) { meshReloadCallback_ = std::move(callback); }
		// 描画アセット変更時のリロード処理を外部から差し込む
		void SetRenderAssetReloadCallback(std::function<void(AssetID)> callback) { renderAssetReloadCallback_ = std::move(callback); }

		// mainスレッドから毎フレーム呼ぶ、内部で数フレームに一度だけ走査し書き込み完了を待ってから反映する
		void Update();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 安定した変更パスを種別ごとにリロードへ振り分ける、内容リロードとして処理できたらtrueを返す
		bool DispatchReload(const std::filesystem::path& path);

		//--------- variables ----------------------------------------------------

		std::vector<std::unique_ptr<AssetChangeWatcher>> watchers_;
		AssetDatabase* assetDatabase_ = nullptr;
		TextureUploadService* textureUploadService_ = nullptr;
		// モデル変更時に呼ぶリロードでmesh管理がbackend内にあるため間接化する
		std::function<void(AssetID)> meshReloadCallback_;
		// Material/Shader/Pipeline/Font変更時に描画側へ通知する
		std::function<void(AssetID)> renderAssetReloadCallback_;

		// 変更検知の間引き用フレームカウンタ
		uint32_t frameCounter_ = 0;
		// 変更パスごとの最後に検知した時刻でdebounceに使い、書き込み途中のファイルを読まないようにする
		std::unordered_map<std::filesystem::path,
			std::chrono::steady_clock::time_point> pendingChanges_;
	};
} // Engine
