#include "ShaderCook.h"

//============================================================================
//	include
//============================================================================
#include "ShaderCookStorage.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {

	using namespace Engine::ShaderCookStorage;
	constexpr const char* kShaderCookManifest =
		"Cooked/Shaders/ShaderCookManifest.json";

	struct CookedStageRecord {

		Engine::ShaderCookRequest request{};
		std::filesystem::path bytecodePath;
		Engine::ShaderReflectionInfo reflection{};
	};

	struct CookedShaderRecord {

		Engine::ShaderAsset asset{};
		std::vector<CookedStageRecord> stages;
	};

	struct ShaderCookRuntimeState {

		std::filesystem::path manifestPath;
		std::unordered_map<Engine::AssetID, CookedShaderRecord> shaders;
		std::unordered_map<Engine::AssetID, Engine::RenderPipelineAsset> pipelines;
		bool loaded = false;
		bool valid = false;
	};

	ShaderCookRuntimeState g_runtimeState{};
	std::mutex g_runtimeMutex;

	bool LoadRuntimeManifest() {

		const std::filesystem::path manifestPath =
			Engine::RuntimePaths::GetGameRoot() / kShaderCookManifest;
		if (g_runtimeState.loaded &&
			g_runtimeState.manifestPath == manifestPath) {
			return g_runtimeState.valid;
		}

		g_runtimeState = ShaderCookRuntimeState{};
		g_runtimeState.loaded = true;
		g_runtimeState.manifestPath = manifestPath;
		// Source実行ではCook済みデータを参照せず、呼び出し側のHLSLコンパイルへ戻す
		if (!Engine::ShaderCook::IsCookedProduct()) {
			return false;
		}
		const nlohmann::json manifest =
			Engine::JsonAdapter::Load(manifestPath, true);
		if (!manifest.is_object() ||
			manifest.value("schemaVersion", 0u) != kShaderCookSchemaVersion ||
			!manifest.value("cookedOnly", false)) {
			return false;
		}
		for (const nlohmann::json& item :
			manifest.value("shaders", nlohmann::json::array())) {
			CookedShaderRecord record{};
			if (!item.is_object() ||
				!ReadShaderMetadata(item.value("asset", nlohmann::json{}), record.asset)) {
				return false;
			}
			for (const nlohmann::json& stageData :
				item.value("stages", nlohmann::json::array())) {
				CookedStageRecord stage{};
				stage.request.shader = record.asset.guid;
				stage.request.stage = Engine::EnumAdapter<Engine::ShaderStage>::FromString(
					stageData.value("stage", "None")).value_or(Engine::ShaderStage::None);
				stage.request.entry = stageData.value("entry", "main");
				stage.request.profile = stageData.value("profile", "");
				stage.bytecodePath = manifestPath.parent_path() /
					Engine::Algorithm::PathFromUTF8(stageData.value("bytecode", ""));
				if (stage.request.stage == Engine::ShaderStage::None ||
					stage.bytecodePath.filename().empty() ||
					!ReadReflection(stageData.value("reflection", nlohmann::json{}), stage.reflection)) {
					return false;
				}
				record.stages.emplace_back(std::move(stage));
			}
			g_runtimeState.shaders.emplace(record.asset.guid, std::move(record));
		}
		for (const nlohmann::json& item :
			manifest.value("pipelines", nlohmann::json::array())) {
			Engine::RenderPipelineAsset pipeline{};
			if (!item.is_object() || !Engine::FromJson(item, pipeline)) {
				return false;
			}
			pipeline.guid = Engine::FromString32Hex(item.value("guid", ""));
			if (!pipeline.guid) {
				return false;
			}
			g_runtimeState.pipelines.emplace(pipeline.guid, std::move(pipeline));
		}
		g_runtimeState.valid = !g_runtimeState.shaders.empty();
		return g_runtimeState.valid;
	}
}

bool Engine::ShaderCook::IsCookedProduct() {

	return RuntimePaths::IsProductBuild();
}

bool Engine::ShaderCook::LoadShaderAsset(AssetID shaderID,
	ShaderAsset& outAsset) {

	std::scoped_lock lock(g_runtimeMutex);
	if (!LoadRuntimeManifest()) {
		return false;
	}
	const auto found = g_runtimeState.shaders.find(shaderID);
	if (found == g_runtimeState.shaders.end()) {
		return false;
	}
	outAsset = found->second.asset;
	return true;
}

bool Engine::ShaderCook::LoadPipelineAsset(AssetID pipelineID,
	RenderPipelineAsset& outAsset) {

	std::scoped_lock lock(g_runtimeMutex);
	if (!LoadRuntimeManifest()) {
		return false;
	}
	const auto found = g_runtimeState.pipelines.find(pipelineID);
	if (found == g_runtimeState.pipelines.end()) {
		return false;
	}
	outAsset = found->second;
	return true;
}

bool Engine::ShaderCook::Load(const ShaderCookRequest& request,
	CompiledShader& outShader) {

	std::scoped_lock lock(g_runtimeMutex);
	if (!LoadRuntimeManifest()) {
		return false;
	}
	const auto shader = g_runtimeState.shaders.find(request.shader);
	if (shader == g_runtimeState.shaders.end()) {
		return false;
	}
	const auto stage = std::find_if(shader->second.stages.begin(),
		shader->second.stages.end(), [&](const CookedStageRecord& record) {
			return record.request.stage == request.stage &&
				record.request.entry == request.entry &&
				record.request.profile == request.profile;
		});
	if (stage == shader->second.stages.end()) {
		return false;
	}
	outShader = CompiledShader{};
	outShader.stage = request.stage;
	outShader.entry = Algorithm::ConvertString(request.entry);
	outShader.profile = Algorithm::ConvertString(request.profile);
	outShader.reflection = stage->reflection;
	return ReadBinary(stage->bytecodePath, outShader.bytecode);
}
