#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Controllers/AnimationControllerManager.h>
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Animation/AnimationControllerPlayback.h>

// c++
#include <algorithm>

namespace {

	Engine::AnimationPlayerComponent* ResolvePlayer(Engine::ManagedNativeEntity entity,
		const Engine::SystemContext* context, const char* name, int32_t type) {

		using namespace Engine;
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!context || !context->assetDatabase || !context->animationControllerManager ||
			!world || !world->IsAlive(resolved) || !name || !*name) return nullptr;
		auto* player = world->TryGetComponentForBinding<AnimationPlayerComponent>(resolved);
		if (!player || !player->controller) return nullptr;
		// Awakeや追加直後の呼出しにも初期Parameterを用意する
		AnimationControllerPlayback::Synchronize(*player, *context);
		const auto* definition = context->animationControllerManager->GetOrLoad(*context->assetDatabase, player->controller);
		if (!definition) return nullptr;
		const auto parameter = std::find_if(definition->asset.parameters.begin(), definition->asset.parameters.end(), [&](const auto& parameter) {
			return parameter.name == name && static_cast<int32_t>(parameter.type) == type;
		});
		return parameter != definition->asset.parameters.end() ? player : nullptr;
	}
}

//============================================================================
//	ManagedScriptRuntime animationMethods
//============================================================================
int32_t Engine::ManagedScriptRuntime::SetAnimatorParameterCallback(ManagedNativeEntity entity,
	const char* name, int32_t type, float number, int32_t integer) {

	const SystemContext* context = GetCurrentContext();
	auto* player = ResolvePlayer(entity, context, name, type);
	if (!player) return 0;
	AnimationControllerParameterValue value;
	switch (static_cast<AnimationControllerParameterType>(type)) {
	case AnimationControllerParameterType::Float: value = number; break;
	case AnimationControllerParameterType::Integer: value = integer; break;
	case AnimationControllerParameterType::Boolean:
	case AnimationControllerParameterType::Trigger: value = integer != 0; break;
	default: return 0;
	}
	const auto* definition = context->animationControllerManager->GetOrLoad(*context->assetDatabase, player->controller);
	return AnimationControllerEvaluator::SetParameter(definition->asset, player->runtimeController, name, value) ? 1 : 0;
}

int32_t Engine::ManagedScriptRuntime::GetAnimatorParameterCallback(ManagedNativeEntity entity,
	const char* name, int32_t type, float* number, int32_t* integer) {

	if (!number || !integer) return 0;
	*number = 0.0f;
	*integer = 0;
	auto* player = ResolvePlayer(entity, GetCurrentContext(), name, type);
	if (!player) return 0;
	const auto* value = AnimationControllerEvaluator::GetParameter(player->runtimeController, name);
	if (!value) return 0;
	// 可変長の定義を境界へ渡さず指定型の値だけを複写する
	if (const auto* floatValue = std::get_if<float>(value)) *number = *floatValue;
	else if (const auto* integerValue = std::get_if<int32_t>(value)) *integer = *integerValue;
	else *integer = std::get<bool>(*value) ? 1 : 0;
	return 1;
}
