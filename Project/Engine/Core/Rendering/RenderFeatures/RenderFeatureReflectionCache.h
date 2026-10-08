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
			const std::vector<ShaderConstantBufferVariable>& variables, const std::vector<ShaderResourceBinding>& resources,
			const std::vector<ShaderResourceBinding>& samplers);

		// Materialの型情報を破棄する
		void ClearReflection(AssetID materialID);

		// 全Shaderの型情報を破棄する
		void ClearReflectionCache();

		//--------- accessor -----------------------------------------------------

		const std::vector<ShaderConstantBufferVariable>* FindReflectionVariables(
			AssetID materialID, MaterialPassKind passKind) const;

		const std::vector<ShaderResourceBinding>* FindReflectionResources(AssetID materialID, MaterialPassKind passKind) const;

		const std::vector<ShaderResourceBinding>* FindReflectionSamplers(AssetID materialID, MaterialPassKind passKind) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// MaterialとShader用途の識別子
		struct ReflectionKey {

			AssetID material{};									   // 参照元のMaterial
			MaterialPassKind passKind = MaterialPassKind::Invalid; // Shaderの用途

			bool operator==(const ReflectionKey&) const = default;
		};

		// 型情報の検索に使うHash
		struct ReflectionKeyHash {

			// Materialと用途を合わせてHash化する
			size_t operator()(const ReflectionKey& key) const noexcept;
		};

		// 同じShaderから取得した型情報
		struct ReflectionData {

			std::vector<ShaderConstantBufferVariable> variables{}; // 定数の型情報
			std::vector<ShaderResourceBinding> resources{};		   // 資源の割当
			std::vector<ShaderResourceBinding> samplers{};		   // サンプラーの割当
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<ReflectionKey, ReflectionData, ReflectionKeyHash> entries_{}; // Shader単位の型情報
	};
}
