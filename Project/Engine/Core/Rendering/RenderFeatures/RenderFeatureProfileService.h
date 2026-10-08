#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileDocument.h"
#include "RenderFeatureReflectionCache.h"
#include "RenderPassesAsset.h"
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>

// c++
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	RenderFeatureProfileService class
	//	アクティブなRenderFeatureProfileと編集用キャッシュを管理するクラス
	//============================================================================
	class RenderFeatureProfileService final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileService(const RenderFeatureProfileService&) = delete;
		RenderFeatureProfileService& operator=(const RenderFeatureProfileService&) = delete;

		// 未読込の編集設定を取得する
		void EnsureLoaded();
		// 保存先から編集設定を取得する
		bool Load();
		// 未保存値を保存先の内容へ戻す
		bool Reload();
		// 現在の編集設定を保存する
		bool Save() const;
		// 読込成功後に編集対象を切り替える
		bool SetActiveProfileAsset(AssetID assetID, const AssetDatabase* assetDatabase);
		// Main CameraのRender PassesをC#操作用へ同期する
		void SetRuntimeExtension(const RenderPassesAsset* extension, uint64_t revision = 0);
		// 保存先を指定して編集対象を切り替える
		bool SetActiveProfilePath(const std::filesystem::path& path);
		// 編集内容から実行構成を作り直す
		void RebuildRuntime();

		// Shader単位で型情報を保持する
		void CacheReflection(AssetID materialID, MaterialPassKind passKind,
			const std::vector<ShaderConstantBufferVariable>& variables, const std::vector<ShaderResourceBinding>& resources,
			const std::vector<ShaderResourceBinding>& samplers);
		// Materialに属する型情報を破棄する
		void ClearReflection(AssetID materialID);
		// 全Shaderの型情報を破棄する
		void ClearReflectionCache();

		//--------- accessor -----------------------------------------------------

		RenderFeatureProfileAsset& GetProfile() { return document_.profile_; }
		const RenderFeatureProfileAsset& GetProfile() const { return document_.profile_; }
		const RenderFeatureProfileRuntime& GetRuntime() const { return runtime_; }
		uint64_t GetRuntimeGeneration() const { return runtimeGeneration_; }
		const RenderFeaturePassSettings* FindPassByID(UUID passID) const;
		const RenderFeaturePassSettings* FindPassByName(std::string_view passName) const;
		const RenderFeatureProfileRuntime& GetRuntimeExtension() const { return runtimeExtensionRuntime_; }
		uint64_t GetRuntimeExtensionGeneration() const { return runtimeExtensionGeneration_; }
		const RenderFeaturePassSettings* FindRuntimeExtensionPassByID(UUID passID) const;
		const RenderFeaturePassSettings* FindRuntimeExtensionPassByName(std::string_view passName) const;
		const std::filesystem::path& GetCurrentPath() const { return document_.profilePath_; }
		bool IsDirty() const { return document_.dirty_; }
		void MarkDirty() { document_.dirty_ = true; }
		void ClearDirty() { document_.dirty_ = false; }

		const std::vector<ShaderConstantBufferVariable>* FindReflectionVariables(
			AssetID materialID, MaterialPassKind passKind) const;
		const std::vector<ShaderResourceBinding>* FindReflectionResources(AssetID materialID, MaterialPassKind passKind) const;
		const std::vector<ShaderResourceBinding>* FindReflectionSamplers(AssetID materialID, MaterialPassKind passKind) const;

		static RenderFeatureProfileService& GetInstance();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		RenderFeatureProfileDocument document_{};				// 編集設定と保存状態
		RenderFeatureProfileRuntime runtime_{};					// 編集用プレビューの実行構成
		uint64_t runtimeGeneration_ = 0;						// 編集構成の世代
		AssetID runtimeExtensionID_{};							// MainCameraの追加Pass
		uint64_t runtimeExtensionRevision_ = 0;					// 追加Passの入力世代
		RenderFeatureProfileRuntime runtimeExtensionRuntime_{}; // C#操作用の実行構成
		uint64_t runtimeExtensionGeneration_ = 0;				// C#操作用の構成世代
		RenderFeatureReflectionCache reflectionCache_{};		// Inspector用のShader型情報

		//--------- functions ----------------------------------------------------

		// 編集用の管理状態を初期化する
		RenderFeatureProfileService() = default;
		// 所有する編集設定とcacheを破棄する
		~RenderFeatureProfileService() = default;

		// 読込と実行構成の作成後に編集状態を切り替える
		bool ReadProfile(const std::filesystem::path& path);
	};
}
