#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RenderFeatureReflectionCache class
	//	Materialの編集に使うShader型情報を保持する
	//============================================================================
	class RenderFeatureReflectionCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 実行Shaderの型情報を保持する
		void CacheReflection(AssetID materialID, MaterialPassKind passKind,
			const std::vector<ShaderConstantBufferVariable>& variables,
			const std::vector<ShaderResourceBinding>& resources, const std::vector<ShaderResourceBinding>& samplers);

		// Materialの型情報を破棄する
		void ClearReflection(AssetID materialID);

		void ClearReflectionCache();

		const std::vector<ShaderConstantBufferVariable>* FindReflectionVariables(AssetID materialID, MaterialPassKind passKind) const;

		const std::vector<ShaderResourceBinding>* FindReflectionResources(AssetID materialID, MaterialPassKind passKind) const;

		const std::vector<ShaderResourceBinding>* FindReflectionSamplers(AssetID materialID, MaterialPassKind passKind) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct ReflectionKey {

			AssetID material{};
			MaterialPassKind passKind = MaterialPassKind::Invalid;

			bool operator==(const ReflectionKey&) const = default;
		};

		struct ReflectionKeyHash {

			size_t operator()(const ReflectionKey& key) const noexcept;
		};

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
}
