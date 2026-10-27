#include "RuntimeAPITests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedMaterialConversion.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>

// c++
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {

	using namespace Engine;
	using namespace Engine::GeneratedComponentBindings;

	// 全公開プロパティの転送幅と書込結果を確認する
	bool CheckBindings() {

		const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
			"Engine/Core/Scripting/Managed/Bindings/ComponentManifest.json";
		const auto manifest = JsonFile::Load(path, false);
		if (!manifest.contains("components")) return false;
		ECSWorld world(ECSWorldKind::Runtime);
		auto& registry = ManagedWorldRegistry::GetInstance();
		const auto handle = registry.Register(world);
		bool passed = true;
		uint32_t checked = 0;
		for (const auto& component : manifest["components"]) {
			if (component.value("exposure", "") != "GeneratedBinding") continue;
			const auto entity = SceneAuthoring::CreateGameObject(world, "BindingContract");
			world.AddComponentByName(entity, component.at("registryName").get<std::string>());
			const auto native = MakeNativeEntity(world, entity);
			const auto type = component.at("id").get<int32_t>();
			for (const auto& property : component.value("properties", nlohmann::json::array())) {
				const auto id = property.at("id").get<int32_t>();
				const auto kind = property.at("kind").get<std::string>();
				const bool writable = property.value("access", "ReadWrite") == "ReadWrite";
				bool valid = true;
				if (kind == "String") {
					int32_t size = 0;
					const auto status = GetComponentStringProperty(native, type, id, nullptr, 0, &size);
					valid = status == ManagedStatus::Ok || status == ManagedStatus::BufferTooSmall;
					if (writable) {
						constexpr char text[] = "確認用";
						valid &= SetComponentStringProperty(native, type, id, text, sizeof(text) - 1) == ManagedStatus::Ok;
						std::array<char, 64> result{};
						valid &= GetComponentStringProperty(native, type, id, result.data(), static_cast<int32_t>(result.size()), &size) == ManagedStatus::Ok;
						valid &= size == sizeof(text) - 1 && std::memcmp(result.data(), text, sizeof(text) - 1) == 0;
					}
				} else {
					alignas(16) std::array<uint8_t, 16> original{}, value{}, result{};
					int size = kind == "Vector2" ? 8 : (kind == "Vector3" || kind == "Color3") ? 12 :
						(kind == "Vector4" || kind == "Color4" || kind == "Quaternion" || kind == "AssetRef" || kind == "EntityRef") ? 16 : 4;
					valid = GetComponentProperty(native, type, id, original.data(), size) == ManagedStatus::Ok;
					value = original;
					if (writable && valid) {
						if (kind == "Bool") { int32_t boolean; std::memcpy(&boolean, value.data(), 4); boolean = !boolean; std::memcpy(value.data(), &boolean, 4); }
						else if (kind == "Float" || kind.starts_with("Vector") || kind.starts_with("Color")) {
							for (int offset = 0; offset < size; offset += 4) { const float number = 0.375f; std::memcpy(value.data() + offset, &number, 4); }
						} else if (kind == "Int" || kind == "UInt") { const uint32_t number = 3; std::memcpy(value.data(), &number, 4); }
						else if (kind == "AssetRef") { const ManagedAssetGUID asset{0x12345678, 0xABCDEF}; std::memcpy(value.data(), &asset, 16); }
						valid &= SetComponentProperty(native, type, id, value.data(), size) == ManagedStatus::Ok;
						valid &= GetComponentProperty(native, type, id, result.data(), size) == ManagedStatus::Ok;
						valid &= std::memcmp(value.data(), result.data(), size) == 0;
						if (kind == "Float" && !property.value("allowNegativeInfinity", false)) {
							const float invalid = std::numeric_limits<float>::quiet_NaN();
							valid &= SetComponentProperty(native, type, id, &invalid, sizeof(invalid)) == ManagedStatus::InvalidArgument;
						}
						SetComponentProperty(native, type, id, original.data(), size);
					}
				}
				if (!valid) std::cerr << "Binding failed: " << component["registryName"] << "." << property["managedName"] << '\n';
				passed &= valid;
				++checked;
			}
			world.DestroyEntity(entity);
		}
		registry.Unregister(handle);
		std::cout << "Runtime API binding fields checked: " << checked << '\n';
		return passed && checked > 300;
	}

	// 予約中のComponentへ設定した値が追加後も残るか確認する
	bool CheckPendingParticleSettings() {

		ECSWorld world(ECSWorldKind::Runtime);
		const auto entity = SceneAuthoring::CreateGameObject(world, "PendingParticle");
		auto& commands = world.GetCommandBuffer();
		const auto type = ComponentTypeRegistry::GetInstance().GetID<ParticleSystemComponent>();
		commands.StageAddComponent(world, entity, type);
		const auto initial = GetParticleSystemSettings(world, entity);
		if (!initial) return false;
		auto settings = *initial;
		settings.looping = false;
		SetParticleSystemSettings(world, entity, settings);
		commands.Flush(world);
		const auto published = GetParticleSystemSettings(world, entity);
		if (!published || published->looping || !world.HasComponent<ParticleSystemComponent>(entity)) return false;
		const auto cancelled = SceneAuthoring::CreateGameObject(world, "CancelledParticle");
		commands.StageAddComponent(world, cancelled, type);
		SetParticleSystemSettings(world, cancelled, settings);
		const auto handle = world.TryGetComponentForBinding<ParticleSystemComponent>(cancelled)->settings;
		commands.EnqueueRemoveComponentByName(cancelled, "ParticleSystem");
		commands.Flush(world);
		return world.GetStorage().Get<ParticleSystemSettingsStorage>().TryGet(handle) == nullptr;
	}

	// 名前指定でShaderGraphの安定IDを維持するか確認する
	bool CheckMaterialNames() {

		using namespace ManagedMaterialConversion;
		MaterialParameterSet parameters;
		const MaterialParameterID stable{0x123456789ABCDEF0};
		MaterialParameterValue value; value.value = 2.0f;
		parameters.Set(stable, "Glow", MaterialParameterSemantic::None, value);
		const auto name = MaterialParameterID::FromName("Glow");
		if (ResolveParameterID(parameters, name) != stable || !FindParameter(parameters, name)) return false;
		ManagedMaterialParameterValue encoded{};
		if (!EncodeMaterialParameterValue(value, encoded)) return false;
		const float invalid = std::numeric_limits<float>::infinity();
		std::memcpy(&encoded.data0, &invalid, sizeof(invalid));
		return !DecodeMaterialParameterValue(encoded, value);
	}

	// Play中の共有Material変更を初回の値へ戻す
	bool CheckRuntimeAssetRestore() {

		AssetDatabase database;
		RenderAssetLibrary library;
		library.Init(&database);
		MaterialAsset original;
		original.guid = AssetID::New();
		original.name = "Original";
		library.RegisterDerivedMaterial(original);
		auto changed = original;
		changed.name = "Runtime1";
		library.SetRuntimeMaterial(changed);
		changed.name = "Runtime2";
		library.SetRuntimeMaterial(changed);
		if (library.LoadMaterial(original.guid)->name != "Runtime2") return false;
		library.ResetRuntimeAssets();
		if (library.LoadMaterial(original.guid)->name != "Original") return false;
		library.ResetRuntimeAssets();
		return library.LoadMaterial(original.guid)->name == "Original";
	}

	// 汎用データとEngine固有のJSONを区別する
	bool CheckTextAssetTypes() {

		return AssetTypeResolver::GuessByPath("Data.csv") == AssetType::Text &&
			AssetTypeResolver::GuessByPath("Data.JSON") == AssetType::Text &&
			AssetTypeResolver::GuessByPath("Test.scene.json") == AssetType::Scene &&
			AssetTypeResolver::GuessByPath("Test.renderpasses.json") == AssetType::RenderPasses &&
			IsAssetTypeCompatible(AssetType::Texture, AssetType::RenderTexture);
	}
}

bool NEMTests::TestRuntimeAPIContracts() {

	return CheckBindings() && CheckPendingParticleSettings() && CheckMaterialNames() &&
		CheckRuntimeAssetRestore() && CheckTextAssetTypes();
}
