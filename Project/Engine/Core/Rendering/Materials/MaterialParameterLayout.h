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
	//	MaterialParameterLayout class
	// Reflectionから指定名の定数バッファのCBVレイアウトだけを切り出して保持する
	// PostProcess/通常マテリアル共通で使うため、対象cbuffer名は呼び出し側が指定する
	//============================================================================
	class MaterialParameterLayout {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		MaterialParameterLayout() = default;
		~MaterialParameterLayout() = default;

		// Reflection内の指定名cbufferからレイアウトを作成する、既定はMaterialParameters
		void Build(const ShaderReflectionInfo& reflection,
			const std::string& cbufferName = "MaterialParameters");

		//--------- accessor -----------------------------------------------------

		bool IsValid() const { return sizeInBytes_ > 0; }
		uint32_t GetSizeInBytes() const { return sizeInBytes_; }
		uint32_t GetBindPoint() const { return bindPoint_; }
		uint32_t GetSpace() const { return space_; }
		const std::vector<ShaderConstantBufferVariable>& GetVariables() const { return variables_; }
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
