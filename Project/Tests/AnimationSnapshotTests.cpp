#include "AnimationSnapshotTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/Animation/Properties/MaterialAnimationPropertyValue.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUtility.h>

// c++
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <fstream>
#include <array>
#include <iostream>
#include <unordered_set>

bool NEMTests::TestAnimationSnapshot() {

	using namespace Engine;
	RegisterBuiltinAnimationProperties();
	// 全標準Componentの公開候補を取得、適用、復元する
	auto& registry = AnimationPropertyRegistry::GetInstance();
	size_t componentCount = 0, propertyCount = 0;
	const auto& componentTypes = ComponentTypeRegistry::GetInstance();
	for (uint32_t index = 0; index < componentTypes.GetComponentTypeCount(); ++index) {
		const auto& info = componentTypes.GetInfo(index);
		if (!info.serializable || info.storageKind != ComponentStorageKind::Data || info.worldDomain == ComponentWorldDomain::Runtime) continue;
		++componentCount;
		ECSWorld fixture;
		const auto target = fixture.CreateEntity();
		fixture.AddComponentByName(target, "Transform");
		if (info.name != "Transform") fixture.AddComponentByName(target, info.name);
		std::unordered_set<std::string> keys;
		for (const auto& property : registry.CollectProperties(fixture, target, {})) {
			++propertyCount;
			const auto key = property.componentName + "." + property.propertyPath;
			AnimationPropertyValue original;
			if (!keys.insert(key).second || !property.getValue || !property.setValue || !property.getValue(fixture, target, original) ||
				original.index() != static_cast<size_t>(property.valueType)) {
				std::cerr << "Animation property read failed: " << key << '\n';
				return false;
			}
			const std::array<AnimationPropertyValue, 8> samples{0.375f, Vector2(0.375f, 0.625f), Vector3(0.375f, 0.625f, 0.875f),
				Vector4(0.375f, 0.625f, 0.875f, 1.0f), Color3(0.375f, 0.625f, 0.875f), Color4(0.375f, 0.625f, 0.875f, 1.0f),
				Quaternion::FromEulerDegrees(Vector3(10, 20, 30)), int32_t(3)};
			const auto& sample = samples[original.index()];
			std::vector<AnimationPreviewBaseValue> snapshot;
			const AnimationPropertyBinding binding{property.componentName, property.propertyPath, property.valueType};
			AnimationPropertyValue actual;
			if (!AnimationPropertySnapshot::Capture(fixture, target, binding, snapshot) ||
				!property.setValue(fixture, target, sample) || !property.getValue(fixture, target, actual) ||
				!AnimationClipEditorUtility::ApproxEqualValue(actual, sample)) {
				std::cerr << "Animation property write failed: " << key << '\n';
				return false;
			}
			AnimationPropertySnapshot::Restore(fixture, target, snapshot);
			if (!property.getValue(fixture, target, actual) || !AnimationClipEditorUtility::ApproxEqualValue(actual, original)) {
				std::cerr << "Animation property restore failed: " << key << '\n';
				return false;
			}
		}
	}
	if (componentCount < 20 || propertyCount < 100) return false;
	std::cout << "Animation property matrix: components=" << componentCount << " properties=" << propertyCount << '\n';
	// アセットの既定色と個別設定を同じ適用先として扱う
	TestDirectory directory("AnimationMaterial", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Color.material.json";
	std::ofstream(path) << R"({"name":"Color","domain":"UI","parameters":{"color":{"r":0.2,"g":0.3,"b":0.4,"a":0.5}},"passes":[]})";
	AssetDatabase database;
	if (!database.Init()) return false;
	const auto material = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Material);
	if (!material) return false;
	ECSWorld materialWorld;
	const auto sprite = materialWorld.CreateEntity();
	materialWorld.AddComponent<SpriteRendererComponent>(sprite).material = material;
	const auto properties = registry.CollectProperties(materialWorld, sprite, {.assetDatabase = &database});
	size_t colors = 0;
	for (const auto& property : properties) if (property.propertyPath == "material.color") {
		++colors;
		AnimationPropertyValue value;
		if (!property.getValue(materialWorld, sprite, value) ||
			!AnimationClipEditorUtility::ApproxEqualValue(value, Color4(0.2f, 0.3f, 0.4f, 0.5f)) ||
			property.setValue(materialWorld, sprite, 1.0f)) return false;
	}
	if (colors != 1) return false;
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
