#include "AnimationPropertyRegistry.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyGroups.h"

void Engine::RegisterBuiltinAnimationProperties() {

	// EditorToolが複数回生成されても、同じPropertyを重複登録しない
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	AnimationPropertyRegistry& registry = AnimationPropertyRegistry::GetInstance();

	// 分野別に登録し、既存の列挙順を維持する
	RegisterTransformAnimationProperties(registry);
	RegisterSpriteAnimationProperties(registry);
	RegisterTextAnimationProperties(registry);
	RegisterLightingAnimationProperties(registry);
	RegisterCameraAnimationProperties(registry);
	RegisterCameraControllerAnimationProperties(registry);
	RegisterRuntimeAnimationProperties(registry);
	RegisterCollisionAnimationProperties(registry);
	RegisterMeshUVAnimationProperties(registry);

	// Materialの個別パラメータはreflectionから解決する
	RegisterMaterialAnimationAccessors();
}
