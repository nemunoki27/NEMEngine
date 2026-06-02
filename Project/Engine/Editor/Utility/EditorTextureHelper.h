#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>

// imgui
#include <imgui.h>
// c++
#include <string>

namespace Engine::EditorTextureHelper {

	constexpr const char* kEditorTextureRoot = "Engine/Assets/Textures/Editor/";

	// Editor専用アイコンは論理パスで扱い、RuntimePaths側で実ファイルへ解決する
	inline std::string MakeEditorTexturePath(const std::string& category, const std::string& filename) {

		std::string path = kEditorTextureRoot;
		path += category;
		if (!path.empty() && path.back() != '/') {
			path += '/';
		}
		path += filename;
		return path;
	}

	// テクスチャがvalidならそのIDを返す。未準備またはvalidでない場合はエラーテクスチャにフォールバックする
	inline ImTextureID GetImTextureID(const TextureUploadService& service, const std::string& key) {

		auto tryGetValid = [&](const std::string& k) -> ImTextureID {
			if (k.empty()) {
				return ImTextureID{};
			}
			if (const auto* tex = service.GetTexture(k)) {
				if (tex->valid) {
					return static_cast<ImTextureID>(tex->gpuHandle.ptr);
				}
			}
			return ImTextureID{};
		};

		if (ImTextureID id = tryGetValid(key); id != ImTextureID{}) {
			return id;
		}
		return tryGetValid(BuiltinTextureLibrary::kErrorTextureKey);
	}
}
