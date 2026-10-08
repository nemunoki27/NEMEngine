#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	マテリアルパラメータcbufferのバインド識別名
	//	シェーダー宣言と一致必須の名前
	//============================================================================
	namespace MaterialParameterCBuffer {

		// Meshの個別マテリアルパラメータを格納する構造化バッファ名
		inline constexpr const char* kMesh = "gMeshMaterialParameters";
		// Sprite/Text等のサーフェスマテリアルパラメータcbuffer名
		inline constexpr const char* kSurface = "MaterialParameters";
	}

	// テクスチャ未設定を表すbindless indexのセンチネル、CPUのpackとシェーダー判定で共有する
	inline constexpr uint32_t kNoTextureIndex = 0xFFFFFFFFu;

	//============================================================================
	//	MaterialParameterLayout class
	//	ReflectionからMaterialのGPU配置を切り出して保持するクラス
	//============================================================================
	class MaterialParameterLayout {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MaterialParameterLayout() = default;
		~MaterialParameterLayout() = default;

		// Reflection内の指定名バッファからレイアウトを作成する
		void Build(const ShaderReflectionInfo& reflection,
			const std::string& cbufferName = MaterialParameterCBuffer::kSurface);

		//--------- accessor -----------------------------------------------------

		bool IsValid() const { return sizeInBytes_ > 0; }
		uint32_t GetSizeInBytes() const { return sizeInBytes_; }
		uint32_t GetBindPoint() const { return bindPoint_; }
		uint32_t GetSpace() const { return space_; }
		const std::vector<ShaderConstantBufferVariable>& GetVariables() const { return variables_; }
		const ShaderConstantBufferVariable* Find(MaterialParameterID id) const;
		// 値の検索方法とGPU配置を含む識別値を取得する
		uint64_t GetContentHash() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		uint32_t sizeInBytes_ = 0; // 1要素のByte数
		uint32_t bindPoint_ = 1; // バインド番号
		uint32_t space_ = 0; // レジスター空間
		std::vector<ShaderConstantBufferVariable> variables_{}; // ID順の変数配置

		//--------- functions ----------------------------------------------------

		// Texture指定を解決し、変数をID順へ並べる
		void PrepareVariables(const ShaderReflectionInfo& reflection);
	};
} // Engine
