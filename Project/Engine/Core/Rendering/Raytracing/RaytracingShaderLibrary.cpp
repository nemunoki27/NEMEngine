#include "RaytracingShaderLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>

// c++
#include <algorithm>
#include <atomic>
#include <cstring>

	Engine::CompiledShader Engine::RaytracingShaderLibrary::Load(
		Engine::DxShaderCompiler* compiler,
		const Engine::ShaderStageEntry& stage) {

		const std::string entry = stage.entry.empty() ? "main" : stage.entry;
		const std::string profile = stage.profile.empty() ? "lib_6_6" : stage.profile;
		Engine::CompiledShader shader{};
		if (stage.ownerShader && Engine::ShaderCook::Load({
			.shader = stage.ownerShader,
			.stage = Engine::ShaderStage::Lib,
			.entry = entry,
			.profile = profile,
			}, shader)) {

			Engine::Logger::Output(Engine::LogType::Engine,
				"[ShaderCook] DXR Shaderの読み込みが完了しました shader={} entry={}",
				Engine::ToString(stage.ownerShader), entry);
			return shader;
		}
		if (Engine::ShaderCook::IsCookedProduct()) {
			Engine::Logger::Output(Engine::LogType::Engine,
				spdlog::level::err,
				"[ShaderCook] Cook済みDXR Shaderが見つかりません shader={} entry={} profile={}",
				Engine::ToString(stage.ownerShader), entry, profile);
			return {};
		}

		const std::filesystem::path path =
			Engine::ShaderSourcePath::Resolve(stage.file);
		if (!compiler || path.empty()) {
			return {};
		}
		return compiler->CompileShader(path.wstring(),
			Engine::Algorithm::ConvertString(profile).c_str(),
			Engine::Algorithm::ConvertString(entry).c_str(),
			Engine::ShaderStage::Lib);
	}
