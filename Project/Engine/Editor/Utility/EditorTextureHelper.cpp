#include "EditorTextureHelper.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>

//============================================================================
//	EditorTextureHelper classMethods
//============================================================================

namespace Engine::EditorTextureHelper {

	std::string MakeEditorTexturePath(const std::string& category, const std::string& filename) {

		std::string path = kEditorTextureRoot;
		path += category;
		if (!path.empty() && path.back() != '/') {
			path += '/';
		}
		path += filename;
		return path;
	}

	ImTextureID GetImTextureID(const TextureUploadService& service, const std::string& key) {

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

	ImTextureID GetSearchIcon(TextureUploadService& service) {

		static const std::string kSearchIconKey = "searchFile.png";
		if (service.GetState(kSearchIconKey) == TextureRequestState::None) {
			service.RequestTextureFile(kSearchIconKey, MakeEditorTexturePath("Project", kSearchIconKey));
		}
		return GetImTextureID(service, kSearchIconKey);
	}
}
