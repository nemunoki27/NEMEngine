#include "PostProcessParameterLayout.h"

//============================================================================
//	PostProcessParameterLayout classMethods
//============================================================================

void Engine::PostProcessParameterLayout::Build(const ShaderReflectionInfo& reflection) {

	sizeInBytes_ = 0;
	bindPoint_ = 1;
	space_ = 0;
	variables_.clear();

	for (const ShaderConstantBufferInfo& buffer : reflection.constantBuffers) {
		if (buffer.name != "PostProcessParameters") {
			continue;
		}

		sizeInBytes_ = buffer.size;
		bindPoint_ = buffer.bindPoint;
		space_ = buffer.space;
		variables_ = buffer.variables;
		return;
	}
}
