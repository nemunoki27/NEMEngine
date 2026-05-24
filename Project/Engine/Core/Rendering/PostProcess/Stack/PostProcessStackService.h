#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackRuntime.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackSettings.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

	//============================================================================
	//	PostProcessStackService class
	//	PostProcessStackの設定をロードし、ランタイムに提供するサービス
	//============================================================================
	class PostProcessStackService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessStackService() = default;
		~PostProcessStackService() = default;

		// 設定が未読み込みなら読み込む
		void EnsureLoaded();
		// 指定したファイルパスから設定を読み込む
		void Load();
		// 現在の設定を保存する
		void Save() const;
		// 現在のパスから再読み込みする
		void Reload();

		// 使用する設定ファイルを論理アセットパスから切り替える
		void SetActiveSettingsAssetPath(const std::string& assetPath);
		// 使用する設定ファイルを実ファイルパスから切り替える
		void SetActiveSettingsPath(const std::filesystem::path& settingsPath);

		// 設定からランタイムデータを再構築する
		void RebuildRuntime();

		// マテリアルのリフレクション情報をキャッシュする
		void CacheReflection(AssetID materialId,
			const std::vector<ShaderConstantBufferVariable>& vars,
			const std::vector<ShaderResourceBinding>& srvBindings);
		// キャッシュ済みCBufferリフレクション変数を取得する
		const std::vector<ShaderConstantBufferVariable>* FindReflectionVars(AssetID materialId) const;
		// キャッシュ済みSRVバインディングを取得する
		const std::vector<ShaderResourceBinding>* FindReflectionSRVs(AssetID materialId) const;
		// 指定マテリアルのリフレクションキャッシュを削除する
		void ClearReflection(AssetID materialId);

		// シェーダーリロードを要求する（次フレームのPostProcessStackPass::Executeで処理される）
		void RequestShaderReload(AssetID materialId);
		// リロード要求を取り出す。存在した場合はtrueを返し要求を削除する
		bool TakeReloadRequest(AssetID materialId);

		//--------- accessor -----------------------------------------------------

		PostProcessStackSettings& GetSettings() { return settings_; }
		const PostProcessStackSettings& GetSettings() const { return settings_; }
		const PostProcessStackRuntime& GetRuntime() const { return runtime_; }
		const std::filesystem::path& GetCurrentPath() const { return settingsPath_; }

		bool IsDirty() const { return dirty_; }
		void MarkDirty() { dirty_ = true; }
		void ClearDirty() { dirty_ = false; }

		// シングルトンインスタンスを取得する
		static PostProcessStackService& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		bool loaded_ = false;
		bool dirty_ = false;

		std::filesystem::path settingsPath_{};
		PostProcessStackSettings settings_{};
		PostProcessStackRuntime runtime_{};

		std::unordered_map<AssetID, std::vector<ShaderConstantBufferVariable>> reflectionVars_{};
		std::unordered_map<AssetID, std::vector<ShaderResourceBinding>> reflectionSRVs_{};
		std::unordered_set<AssetID> pendingReflectionReloads_{};

		//--------- functions ----------------------------------------------------

		std::filesystem::path ResolveDefaultPath() const;
	};
} // Engine
