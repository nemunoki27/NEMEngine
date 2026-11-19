#include "ShaderGraphNodePreviewUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

namespace Engine::ShaderGraphNodePreviewUtility {

	bool IsPreviewableNode(Engine::ShaderGraphNodeKind kind) {

		const Engine::ShaderGraphNodeDescriptor* descriptor = Engine::ShaderGraphNodeRegistry::Find(kind);
		return descriptor && descriptor->preview &&
			(descriptor->stage == Engine::ShaderGraphStage::Any || descriptor->stage == Engine::ShaderGraphStage::Fragment);
	}

	bool IsPreviewableNode(const Engine::ShaderGraphNode& node) {

		return IsPreviewableNode(node.kind) && Engine::GetShaderGraphOutputCount(node) != 0 &&
			(node.stage == Engine::ShaderGraphStage::Any || node.stage == Engine::ShaderGraphStage::Fragment);
	}
} // Engine::ShaderGraphNodePreviewUtility
