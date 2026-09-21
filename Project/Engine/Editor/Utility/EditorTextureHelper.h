#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

// c++
#include <string>

#include <imgui.h>

namespace Engine::EditorTextureHelper {

	constexpr const char* kEditorTextureRoot = "Engine/Assets/Textures/Editor/";

	// Editor専用アイコンは論理パスで扱い、RuntimePaths側で実ファイルへ解決する
	std::string MakeEditorTexturePath(const std::string& category, const std::string& filename);

	// テクスチャがvalidならそのIDを返し未準備またはvalidでない場合はエラーテクスチャにフォールバックする
	ImTextureID GetImTextureID(const TextureUploadService& service, const std::string& key);

	// 検索ボックス共通の虫眼鏡アイコンを取得する、未要求なら要求してから返す
	ImTextureID GetSearchIcon(TextureUploadService& service);
}
