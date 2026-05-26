#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <optional>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	PostProcessParameterLayout class
	//	ReflectionからPostProcessParametersのCBVレイアウトだけを切り出して保持する。
	//============================================================================
	class PostProcessParameterLayout {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessParameterLayout() = default;
		~PostProcessParameterLayout() = default;

		// Reflection内のPostProcessParametersからレイアウトを作成する
		void Build(const ShaderReflectionInfo& reflection);

		//--------- accessor -----------------------------------------------------

		bool IsValid() const { return sizeInBytes_ > 0; }
		uint32_t GetSizeInBytes() const { return sizeInBytes_; }
		uint32_t GetBindPoint() const { return bindPoint_; }
		uint32_t GetSpace() const { return space_; }
		const std::vector<ShaderConstantBufferVariable>& GetVariables() const { return variables_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		uint32_t sizeInBytes_ = 0;
		uint32_t bindPoint_ = 1;
		uint32_t space_ = 0;
		std::vector<ShaderConstantBufferVariable> variables_{};
	};
} // Engine
