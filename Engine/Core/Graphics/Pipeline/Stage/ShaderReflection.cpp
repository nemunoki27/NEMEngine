#include "ShaderReflection.h"

using namespace Engine;

//============================================================================
//	ShaderReflection classMethods
//============================================================================

ShaderStage Engine::operator|(ShaderStage a, ShaderStage b) {

	using T = std::underlying_type_t<ShaderStage>;
	return static_cast<ShaderStage>(static_cast<T>(a) | static_cast<T>(b));
}

ShaderStage& Engine::operator|=(ShaderStage& a, ShaderStage b) {

	a = a | b;
	return a;
}