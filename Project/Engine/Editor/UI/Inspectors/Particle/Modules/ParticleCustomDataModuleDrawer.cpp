#include "ParticleCustomDataModuleDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomDataModule.h>

Engine::ParticleCustomDataModuleDrawer::ParticleCustomDataModuleDrawer() {

	std::vector<ShaderConstantBufferVariable> variables(2);
	for (uint32_t i = 0; i < variables.size(); ++i) {
		variables[i].name = i == 0 ? "CustomData1" : "CustomData2";
		variables[i].valueType = D3D_SVT_FLOAT;
		variables[i].columns = 4;
		variables[i].rows = 1;
		variables[i].size = sizeof(Vector4);
	}
	parameters_.SetReflectedParameters(variables);
}

bool Engine::ParticleCustomDataModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleCustomDataModule*>(&module);
	if (!concrete) return false;
	auto settings = concrete->GetSettings();
	ParticleCustomShaderParameterModule parameters;
	parameters.SetParameters({{"CustomData1", settings.customData1}, {"CustomData2", settings.customData2}});
	if (!parameters_.Draw(parameters)) return false;
	const auto& values = parameters.GetParameters();
	settings.customData1 = values.contains("CustomData1") ? values.at("CustomData1") : ParticleMaterialAnimatedParameter{};
	settings.customData2 = values.contains("CustomData2") ? values.at("CustomData2") : ParticleMaterialAnimatedParameter{};
	settings.customData1.componentCount = settings.customData2.componentCount = 4;
	settings.enabled = true;
	concrete->SetSettings(settings);
	return true;
}
