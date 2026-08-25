#pragma once

//============================================================================
//	include
//============================================================================
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

		RenderFeatureProfileAsset& GetProfile() { return profile_; }
		const RenderFeatureProfileAsset& GetProfile() const { return profile_; }
		const RenderFeatureProfileRuntime& GetRuntime() const { return runtime_; }
		uint64_t GetRuntimeGeneration() const { return runtimeGeneration_; }
		const RenderFeaturePassSettings* FindPassByID(UUID passID) const;
		const RenderFeaturePassSettings* FindPassByName(
			std::string_view passName) const;
		const std::filesystem::path& GetCurrentPath() const { return profilePath_; }
		bool IsDirty() const { return dirty_; }
		void MarkDirty() { dirty_ = true; }
		void ClearDirty() { dirty_ = false; }

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

		struct ReflectionKey {

			AssetID material{};
			MaterialPassKind passKind = MaterialPassKind::Invalid;

			bool operator==(const ReflectionKey&) const = default;
		};

		struct ReflectionKeyHash {

			size_t operator()(const ReflectionKey& key) const noexcept {

				return std::hash<AssetID>{}(key.material) ^
					(std::hash<uint8_t>{}(
						static_cast<uint8_t>(key.passKind)) << 1);
			}
		};

		//--------- variables ----------------------------------------------------

		bool loaded_ = false;
		bool dirty_ = false;
		std::filesystem::path profilePath_{};
		RenderFeatureProfileAsset profile_{};
		RenderFeatureProfileRuntime runtime_{};
		uint64_t runtimeGeneration_ = 0;
		std::unordered_map<ReflectionKey,
			std::vector<ShaderConstantBufferVariable>,
			ReflectionKeyHash> reflectionVariables_{};
		std::unordered_map<ReflectionKey,
			std::vector<ShaderResourceBinding>,
			ReflectionKeyHash> reflectionResources_{};
		std::unordered_map<ReflectionKey,
			std::vector<ShaderResourceBinding>,
			ReflectionKeyHash> reflectionSamplers_{};
	};
} // Engine
