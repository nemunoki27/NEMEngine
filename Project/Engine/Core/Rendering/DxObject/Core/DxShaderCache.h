#pragma once

#include "ShaderSourceIncludeHandler.h"
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

#include <mutex>
#include <span>

namespace Engine {

	// 1つのコンパイル要求に対する検索から保存までをまとめる
	class DxShaderCache {
	public:
		DxShaderCache(const std::filesystem::path& directory, std::string_view compilerHash,
			std::span<LPCWSTR const> arguments, ShaderStage stage, const DxcBuffer& source);
		bool Load(IDxcUtils* utils, CompiledShader& shader);
		void Store(const CompiledShader& shader, IDxcBlob* reflection);

		ShaderSourceDependencies dependencies;
	private:
		std::filesystem::path path_;
		std::string sourceHash_;
		size_t bucket_ = 0;
		std::unique_lock<std::mutex> lock_;
	};
}
