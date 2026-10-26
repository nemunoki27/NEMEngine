#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUProgram.h"

namespace Engine {

	// World参照をSystem単位へ集約し、Phase固有の設定を命令へ保存する
	bool CompileParticleGPUWorldModule(IParticleModule& module, ParticleGPUInstruction& instruction, ParticleGPUProgram& program);
}
