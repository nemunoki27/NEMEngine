#include "DefaultMaterialSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	DefaultMaterialSettings classMethods
//============================================================================
Engine::DefaultMaterialSettings& Engine::DefaultMaterialSettings::GetInstance() {

	static DefaultMaterialSettings instance;
	return instance;
}

bool Engine::DefaultMaterialSettings::Load(const std::string& configPath) {

	// 別Projectの設定を読込失敗へ持ち越さない
	if (configPath_ != configPath) {
		configuration_ = {};
	}
	configPath_ = configPath;
	if (!DefaultMaterialConfigurationIO::Read(Algorithm::PathFromUTF8(configPath_), configuration_)) {

		Logger::Output(LogType::Engine, spdlog::level::warn, "[Material] 既定設定の読み込みに失敗しました path={}", configPath_);
		return false;
	}
	return true;
}

bool Engine::DefaultMaterialSettings::Save() const {

	if (configPath_.empty()) {
		return false;
	}
	// 保存結果を呼出し元へ返す
	if (!DefaultMaterialConfigurationIO::Write(Algorithm::PathFromUTF8(configPath_), configuration_)) {

		Logger::Output(LogType::Engine, spdlog::level::err, "[Material] 既定設定の保存に失敗しました path={}", configPath_);
		return false;
	}
	return true;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetMeshOrBuiltin() const {

	return configuration_.mesh ? configuration_.mesh : BuiltinAssets::Materials::DefaultMesh;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetSpriteOrBuiltin() const {

	return configuration_.sprite ? configuration_.sprite : BuiltinAssets::Materials::DefaultSprite;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetTextOrBuiltin() const {

	return configuration_.text ? configuration_.text : BuiltinAssets::Materials::DefaultText;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetLineOrBuiltin() const {

	return configuration_.line ? configuration_.line : BuiltinAssets::Materials::DefaultLine;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetPrimitiveOrBuiltin() const {

	return configuration_.primitive ? configuration_.primitive : BuiltinAssets::Materials::DefaultPrimitive;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetPrimitive2DOrBuiltin() const {

	return configuration_.primitive2D ? configuration_.primitive2D : BuiltinAssets::Materials::DefaultPrimitive2D;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetRaytracingReflectionOrBuiltin() const {

	return configuration_.raytracingReflection ? configuration_.raytracingReflection : BuiltinAssets::Materials::RaytracingReflection;
}
