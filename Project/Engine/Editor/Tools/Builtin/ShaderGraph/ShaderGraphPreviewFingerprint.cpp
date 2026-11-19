#include "ShaderGraphPreviewFingerprint.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

namespace Engine::ShaderGraphNodePreviewUtility {

	uint64_t CalculatePreviewHash(const Engine::ShaderGraphAsset& graph) {

		auto data = Engine::ToJson(graph);
		data.erase("name");
		data.erase("groups");
		for (auto& node : data["nodes"]) {
			node.erase("position");
			node.erase("groupID");
		}
		return Engine::Algorithm::MixHashString(14695981039346656037ull, data.dump());
	}

	uint64_t CombinePreviewHash(uint64_t seed, uint64_t value) {

		return Engine::Algorithm::MixHash(seed, value);
	}

	uint64_t HashPreviewValue(const Engine::MaterialParameterValue& parameter) {

		return Engine::MaterialParameterHash::HashValue(parameter);
	}
} // Engine::ShaderGraphNodePreviewUtility
