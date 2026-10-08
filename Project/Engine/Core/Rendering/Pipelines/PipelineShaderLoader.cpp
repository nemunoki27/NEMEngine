#include "PipelineShaderLoader.h"

//============================================================================
//	include
//============================================================================
#include "ShaderSourcePathResolver.h"
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace Engine::PipelineShaderLoader {

	std::filesystem::path ResolveShaderPath(const std::string& file) {

		return ShaderSourcePath::Resolve(file);
	}

	std::wstring ResolveEntry(const std::string& entry) {

		return Algorithm::ConvertString(entry.empty() ? std::string("main") : entry);
	}

	std::wstring ResolveProfile(const std::string& profile, ShaderStage stage) {

		if (!profile.empty()) {
			return Algorithm::ConvertString(profile);
		}
		switch (stage) {
		case ShaderStage::VS: return L"vs_6_0";
		case ShaderStage::GS: return L"gs_6_0";
		case ShaderStage::PS: return L"ps_6_0";
		case ShaderStage::CS: return L"cs_6_0";
		case ShaderStage::MS: return L"ms_6_6";
		case ShaderStage::AS: return L"as_6_6";
		default:              return L"";
		}
	}

	bool CompileOne(std::vector<CompiledShader>& shaders, DxShaderCompiler* compiler,
		const ShaderCompileDesc& desc, ShaderStage stage, const char* stageName) {

		if (desc.file.empty()) {
			return true;
		}
		const std::string entryText = desc.entry.empty() ? "main" : desc.entry;
		const std::string profileText = desc.profile.empty() ?
			Algorithm::ConvertString(ResolveProfile({}, stage)) : desc.profile;
		CompiledShader shader{};
		if (desc.shader && ShaderCook::Load({
			.shader = desc.shader,
			.stage = stage,
			.entry = entryText,
			.profile = profileText,
			}, shader)) {
			shaders.emplace_back(std::move(shader));
			Logger::Output(LogType::Engine,
				"[ShaderCook] 読み込み完了 stage={} shader={} entry={}",
				stageName, ToString(desc.shader), entryText);
			return true;
		}
		if (ShaderCook::IsCookedProduct()) {
			Logger::Output(LogType::Engine,
				"[ShaderCook] Cook済みShaderが見つかりません shader={} stage={} entry={} profile={}",
				ToString(desc.shader), stageName, entryText, profileText);
			return false;
		}

		std::filesystem::path shaderPath = ResolveShaderPath(desc.file);
		if (shaderPath.empty()) {
			Logger::Output(LogType::Engine, "Shaderファイルが見つかりません path={}", desc.file);
			return false;
		}

		std::wstring entry = ResolveEntry(desc.entry);
		std::wstring profile = ResolveProfile(desc.profile, stage);

		shader = compiler->CompileShader(shaderPath.wstring(), profile.c_str(), entry.c_str(), stage);
		if (!shader.IsValid()) {
			Logger::Output(LogType::Engine, "Shaderのコンパイルに失敗しました stage={} path={}",
				stageName, Algorithm::PathToUTF8(shaderPath));
			return false;
		}
		shaders.emplace_back(std::move(shader));
		Logger::Output(LogType::Engine, "Shaderのコンパイルが完了しました stage={} path={}",
			stageName, Algorithm::PathToUTF8(shaderPath));
		return true;
	}

	GraphicsCompileResult Compile(DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc) {

		// VS,GS,MSのいずれかとPSをコンパイルする
		GraphicsCompileResult result{};
		switch (desc.type) {
		case PipelineType::Vertex: {

			result.success &= CompileOne(result.shaders, compiler, desc.preRaster, ShaderStage::VS, "VS");
			result.success &= CompileOne(result.shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			break;
		}
		case PipelineType::Geometry: {

			result.success &= CompileOne(result.shaders, compiler, desc.preRaster, ShaderStage::VS, "VS");
			result.success &= CompileOne(result.shaders, compiler, desc.geometry, ShaderStage::GS, "GS");
			result.success &= CompileOne(result.shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			break;
		}
		case PipelineType::Mesh: {

			result.success &= CompileOne(result.shaders, compiler, desc.preRaster, ShaderStage::MS, "MS");
			result.success &= CompileOne(result.shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			if (!desc.amplification.file.empty()) {
				result.success &= CompileOne(result.shaders, compiler, desc.amplification, ShaderStage::AS, "AS");
			}
			break;
		}
		}
		return result;
	}

	std::vector<const CompiledShader*> MakeShaderPointers(const std::vector<CompiledShader>& shaders) {

		std::vector<const CompiledShader*> result{};
		result.reserve(shaders.size());
		for (const auto& shader : shaders) {
			result.emplace_back(&shader);
		}
		return result;
	}
}
