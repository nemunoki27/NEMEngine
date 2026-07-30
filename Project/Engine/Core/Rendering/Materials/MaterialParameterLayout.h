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
	// Reflectionから指定名のマテリアルパラメータレイアウトを切り出して保持する
	// 定数バッファと構造化バッファの両方で使用する
	//============================================================================
	class MaterialParameterLayout {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

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
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		uint32_t sizeInBytes_ = 0;
		uint32_t bindPoint_ = 1;
		uint32_t space_ = 0;
		std::vector<ShaderConstantBufferVariable> variables_{};
	};
} // Engine
