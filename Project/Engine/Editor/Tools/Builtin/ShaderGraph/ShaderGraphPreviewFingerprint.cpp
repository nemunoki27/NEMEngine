#include "ShaderGraphPreviewFingerprint.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterHash.h>
namespace Engine::ShaderGraphNodePreviewUtility {

	// 値と接続からプレビューの更新を判定する
	uint64_t CalculatePreviewHash(const Engine::ShaderGraphAsset& graph) {

		uint64_t hash = 14695981039346656037ull;
		for (const Engine::ShaderGraphParameter& parameter : graph.parameters) {

			hash = CombinePreviewHash(hash, parameter.id.value);
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(parameter.type));
			hash = CombinePreviewHash(hash, HashPreviewValue(parameter.defaultValue));
		}
		for (const Engine::ShaderGraphNode& node : graph.nodes) {

			hash = CombinePreviewHash(hash, node.id.value);
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.kind));
			hash = CombinePreviewHash(hash, node.parameterID.value);
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.valueType));
			hash = CombinePreviewHash(hash, HashPreviewValue(node.value));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.filter));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.addressU));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.addressV));
			hash = CombinePreviewHash(hash, static_cast<uint64_t>(node.sampler.addressW));
			hash = CombinePreviewHash(hash, node.sampler.maxAnisotropy);
		}
		for (const Engine::ShaderGraphLink& link : graph.links) {

			hash = CombinePreviewHash(hash, link.outputNode.value);
			hash = CombinePreviewHash(hash, link.outputSlot);
			hash = CombinePreviewHash(hash, link.inputNode.value);
			hash = CombinePreviewHash(hash, link.inputSlot);
		}
		return hash;
	}

	// 更新判定用のハッシュへ値を加える
	uint64_t CombinePreviewHash(uint64_t seed, uint64_t value) {

		return Algorithm::MixHash(seed, value);
	}

	// 設定値の型と内容からハッシュを作る
	uint64_t HashPreviewValue(const Engine::MaterialParameterValue& parameter) {

		return MaterialParameterHash::HashValue(parameter);
	}
} // Engine::ShaderGraphNodePreviewUtility
