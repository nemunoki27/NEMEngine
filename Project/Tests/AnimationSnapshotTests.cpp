#include "AnimationSnapshotTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/Animation/Properties/MaterialAnimationPropertyValue.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <array>

bool NEMTests::TestAnimationSnapshot() {

	using namespace Engine;
	RegisterMaterialAnimationAccessors();
	ECSWorld world;
	const Entity entity = world.CreateEntity();
	world.AddComponent<MeshRendererComponent>(entity);
	std::array<SubMeshMaterial, 3> subMeshes;
	subMeshes[0].materialInstance.Set("Metallic", MaterialParameterValue{0.2f});
	subMeshes[1].materialInstance.Set("Metallic", MaterialParameterValue{0.8f});
	SetMeshSubMeshes(world, entity, subMeshes);
	const AnimationPropertyBinding binding{"MeshRenderer", "allMesh.material.Metallic", AnimationValueType::Float};
	std::vector<AnimationPreviewBaseValue> values;
	if (!AnimationPropertySnapshot::Capture(world, entity, binding, values) || values.size() != 4) {
		return false;
	}
	const auto property = AnimationPropertyRegistry::GetInstance().ResolveProperty(
		world, entity, binding.componentName, binding.propertyPath, binding.valueType);
	if (!property || !property->setValue(world, entity, 1.0f)) {
		return false;
	}
	// 一括変更後も個別の値と未設定へ戻す
	AnimationPropertySnapshot::Restore(world, entity, values);
	const auto restored = GetMeshSubMeshes(world, entity);
	if (std::get<float>(restored[0].materialInstance.Find(MaterialParameterID::FromName("Metallic"))->value) != 0.2f ||
		std::get<float>(restored[1].materialInstance.Find(MaterialParameterID::FromName("Metallic"))->value) != 0.8f ||
		restored[2].materialInstance.Find(MaterialParameterID::FromName("Metallic"))) {
		return false;
	}
	// 同じBindingの捕捉を繰り返しても復元値を増やさない
	AnimationPropertySnapshot::Capture(world, entity, binding, values);
	if (values.size() != 4) {
		return false;
	}

	// Color3をMaterialの3成分値として往復する
	const auto color = AnimationPropertyRegistry::GetInstance().ResolveProperty(
		world, entity, "MeshRenderer", "subMeshes[1].material.CustomColor", AnimationValueType::Color3);
	if (!color || color->hasValue(world, entity) || !color->setValue(world, entity, Color3(0.2f, 0.3f, 0.4f))) {
		return false;
	}
	AnimationPropertyValue read;
	if (!color->getValue(world, entity, read)) {
		return false;
	}
	const auto* converted = std::get_if<Color3>(&read);
	if (!converted || converted->r != 0.2f || converted->g != 0.3f || converted->b != 0.4f ||
		!color->clearValue(world, entity) || color->hasValue(world, entity)) {
		return false;
	}

	// 型が合わない値を読み出した場合は失敗を返す
	if (MaterialValueToAnimation(MaterialParameterValue{int32_t{1}}, AnimationValueType::Float, read)) {
		return false;
	}

	// Component削除後の参照は借用先を再確認する
	world.RemoveComponent<MeshRendererComponent>(entity);
	return !color->hasComponent(world, entity) && !color->getValue(world, entity, read) &&
		   !color->setValue(world, entity, Color3::White()) && !color->clearValue(world, entity);
}
