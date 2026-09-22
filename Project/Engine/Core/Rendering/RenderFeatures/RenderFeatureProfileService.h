#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfileDocument.h"
#include "RenderFeatureReflectionCache.h"
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>

// c++
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <unordered_map>
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

		RenderFeatureProfileService(
			const RenderFeatureProfileService&) = delete;
		RenderFeatureProfileService& operator=(
			const RenderFeatureProfileService&) = delete;

		void EnsureLoaded();
		void Load();
		void Reload();
		bool Save() const;
		void SetActiveProfileAsset(AssetID assetID,
			const AssetDatabase* assetDatabase);
		void SetActiveProfilePath(
			const std::filesystem::path& path);
		void RebuildRuntime();

		void CacheReflection(AssetID materialID, MaterialPassKind passKind,
			const std::vector<ShaderConstantBufferVariable>& variables,
			const std::vector<ShaderResourceBinding>& resources,
			const std::vector<ShaderResourceBinding>& samplers);
		void ClearReflection(AssetID materialID);
		void ClearReflectionCache();

		//--------- accessor -----------------------------------------------------

		RenderFeatureProfileAsset& GetProfile() { return document_.profile_; }
		const RenderFeatureProfileAsset& GetProfile() const { return document_.profile_; }
		const RenderFeatureProfileRuntime& GetRuntime() const { return runtime_; }
		uint64_t GetRuntimeGeneration() const { return runtimeGeneration_; }
		const RenderFeaturePassSettings* FindPassByID(UUID passID) const;
		const RenderFeaturePassSettings* FindPassByName(
			std::string_view passName) const;
		const std::filesystem::path& GetCurrentPath() const { return document_.profilePath_; }
		bool IsDirty() const { return document_.dirty_; }
		void MarkDirty() { document_.dirty_ = true; }
		void ClearDirty() { document_.dirty_ = false; }

		const std::vector<ShaderConstantBufferVariable>* FindReflectionVariables(
			AssetID materialID, MaterialPassKind passKind) const;
		const std::vector<ShaderResourceBinding>* FindReflectionResources(
			AssetID materialID, MaterialPassKind passKind) const;
		const std::vector<ShaderResourceBinding>* FindReflectionSamplers(
			AssetID materialID, MaterialPassKind passKind) const;

		static RenderFeatureProfileService& GetInstance();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		RenderFeatureProfileService() = default;
		~RenderFeatureProfileService() = default;

		//--------- variables ----------------------------------------------------

		RenderFeatureProfileDocument document_{};
		RenderFeatureProfileRuntime runtime_{};
		uint64_t runtimeGeneration_ = 0;
		RenderFeatureReflectionCache reflectionCache_{};
	};
} // Engine
