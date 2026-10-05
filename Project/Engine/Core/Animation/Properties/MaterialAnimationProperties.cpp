#include "AnimationPropertyRegistry.h"
#include "MaterialAnimationPropertyValue.h"
#include "MaterialAnimationPropertyCatalog.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>

// c++
#include <format>

//============================================================================
//	MaterialAnimationProperties internal
//============================================================================
namespace {

	using OverridesMap = Engine::MaterialParameterSet;
	using OverridesLocator = std::function<OverridesMap*(Engine::ECSWorld&, const Engine::Entity&)>;

	// Materialの参照先とパラメータ名を分ける
	bool SplitMaterialParamPath(std::string_view path, std::string& outPrefix, std::string& outName) {

		constexpr std::string_view kToken = "material.";
		const size_t pos = path.find(kToken);
		if (pos == std::string_view::npos) {
			return false;
		}
		if (pos == 0) {
			outPrefix.clear();
		} else {
			// tokenの直前は区切りのドットでなければならない
			if (path[pos - 1] != '.') {
				return false;
			}
			outPrefix = std::string(path.substr(0, pos - 1));
		}
		outName = std::string(path.substr(pos + kToken.size()));
		return !outName.empty();
	}

	// "subMeshes[N]" からインデックスNを取り出す
	bool ParseSubMeshIndex(std::string_view prefix, uint32_t& outIndex) {

		const size_t lb = prefix.find('[');
		const size_t rb = prefix.find(']');
		if (lb == std::string_view::npos || rb == std::string_view::npos || rb <= lb + 1) {
			return false;
		}
		uint32_t value = 0;
		for (size_t i = lb + 1; i < rb; ++i) {
			const char c = prefix[i];
			if (c < '0' || c > '9') {
				return false;
			}
			value = value * 10u + static_cast<uint32_t>(c - '0');
		}
		outIndex = value;
		return true;
	}

	// 個別パラメータの取得と適用を登録する
	Engine::AnimationPropertyDescriptor MakeMaterialParamDescriptor(std::string componentName, std::string propertyPath,
		std::string displayName, Engine::AnimationValueType valueType, std::string paramName, OverridesLocator locator) {

		Engine::AnimationPropertyDescriptor desc{};
		desc.componentName = std::move(componentName);
		desc.propertyPath = std::move(propertyPath);
		desc.displayName = std::move(displayName);
		desc.valueType = valueType;
		desc.hasComponent = [locator](Engine::ECSWorld& world, const Engine::Entity& entity) {
			return locator(world, entity) != nullptr;
		};
		desc.getValue = [locator, paramName, valueType](
							Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {
			OverridesMap* map = locator(world, entity);
			if (!map) {
				return false;
			}
			auto it = map->find(paramName);
			if (it != map->end() && Engine::MaterialValueToAnimation(it->second, valueType, out)) {
				return true;
			}
			// 未設定のパラメータは初期値を返す
			out = Engine::ZeroAnimationValue(valueType);
			return true;
		};
		desc.setValue = [locator, paramName](Engine::ECSWorld& world, const Engine::Entity& entity,
							const Engine::AnimationPropertyValue& value) {
			OverridesMap* map = locator(world, entity);
			if (!map) {
				return false;
			}
			map->Set(paramName, Engine::AnimationValueToMaterial(value));
			world.MarkRenderDataModified(entity);
			return true;
		};
		// Preview前の未設定状態へ戻せるようにする
		desc.hasValue = [locator, paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {
			OverridesMap* map = locator(world, entity);
			return map && map->find(paramName) != map->end();
		};
		desc.clearValue = [locator, paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {
			OverridesMap* map = locator(world, entity);
			if (!map) {
				return false;
			}
			if (map->erase(paramName) != 0) {
				world.MarkRenderDataModified(entity);
			}
			return true;
		};
		return desc;
	}

	// Materialの参照先と表示名
	struct MaterialSlotInfo {

		std::string pathPrefix;
		std::string displayPrefix;
		Engine::AssetID material{};
	};

	//============================================================================
	//	Component別のMaterial参照と上書き値を解決する
	//============================================================================
	struct MaterialSlotProvider {

		std::string componentName;
		// Material定数Buffer名
		std::string cbufferName;
		// 編集対象のMaterialを列挙する
		std::function<std::vector<MaterialSlotInfo>(Engine::ECSWorld&, const Engine::Entity&)> enumerateSlots;
		// 実行対象の上書き値を取得する
		std::function<OverridesMap*(Engine::ECSWorld&, const Engine::Entity&, std::string_view)> locateOverrides;
	};

	// 参照先とパラメータ名から保存用のパスを作る
	std::string MakeMaterialParamPath(const std::string& prefix, const std::string& name) {

		return prefix.empty() ? std::format("material.{}", name) : std::format("{}.material.{}", prefix, name);
	}
	std::string MakeMaterialParamDisplay(const std::string& componentName, const std::string& prefix, const std::string& name) {

		return prefix.empty() ? std::format("{}.{}", componentName, name)
							  : std::format("{}.{}.{}", componentName, prefix, name);
	}

	// Materialごとに編集可能なパラメータを列挙する
	void EnumerateMaterialProperties(const MaterialSlotProvider& provider, const Engine::AnimationPropertyQueryContext& context,
		Engine::ECSWorld& world, const Engine::Entity& entity, std::vector<Engine::AnimationPropertyDescriptor>& out) {

		for (const MaterialSlotInfo& slot : provider.enumerateSlots(world, entity)) {

			const auto params = Engine::CollectMaterialAnimationParameters(context, slot.material, provider.cbufferName);

			for (const auto& [name, type] : params) {

				const std::string componentName = provider.componentName;
				const auto& locateOverrides = provider.locateOverrides;
				const std::string pathPrefix = slot.pathPrefix;
				out.emplace_back(MakeMaterialParamDescriptor(componentName, MakeMaterialParamPath(slot.pathPrefix, name),
					MakeMaterialParamDisplay(componentName, slot.displayPrefix, name), type, name,
					[locateOverrides, pathPrefix](Engine::ECSWorld& world, const Engine::Entity& entity) {
						return locateOverrides(world, entity, pathPrefix);
					}));
			}
		}
	}

	// 保存したパスから実行用の参照を作る
	std::optional<Engine::AnimationPropertyDescriptor> ResolveMaterialProperty(
		const MaterialSlotProvider& provider, std::string_view propertyPath, Engine::AnimationValueType valueType) {

		std::string prefix{};
		std::string name{};
		if (!SplitMaterialParamPath(propertyPath, prefix, name)) {
			return std::nullopt;
		}
		const std::string componentName = provider.componentName;
		const auto& locateOverrides = provider.locateOverrides;
		return MakeMaterialParamDescriptor(componentName, std::string(propertyPath),
			MakeMaterialParamDisplay(componentName, prefix, name), valueType, name,
			[locateOverrides, prefix](
				Engine::ECSWorld& world, const Engine::Entity& entity) { return locateOverrides(world, entity, prefix); });
	}

	// Component別のMaterial参照を登録する
	void RegisterMaterialSlotProvider(Engine::AnimationPropertyRegistry& registry, MaterialSlotProvider provider) {

		Engine::MaterialAnimationAccessor accessor{};
		accessor.componentName = provider.componentName;
		accessor.enumerate = [provider](const Engine::AnimationPropertyQueryContext& context, Engine::ECSWorld& world,
								 const Engine::Entity& entity, std::vector<Engine::AnimationPropertyDescriptor>& out) {
			EnumerateMaterialProperties(provider, context, world, entity, out);
		};
		accessor.resolve = [provider](Engine::ECSWorld&, const Engine::Entity&, std::string_view propertyPath,
							   Engine::AnimationValueType valueType) {
			return ResolveMaterialProperty(provider, propertyPath, valueType);
		};
		registry.RegisterMaterialAccessor(accessor);
	}

	//============================================================================
	//	Component別プロバイダ
	//============================================================================
	// MeshRendererはsubMeshes[N]ごとに別々のmaterialInstanceを持つ
	MaterialSlotProvider MakeMeshProvider() {

		MaterialSlotProvider provider{};
		provider.componentName = "MeshRenderer";
		provider.cbufferName = Engine::MaterialParameterCBuffer::kMesh;
		provider.enumerateSlots = [](Engine::ECSWorld& world, const Engine::Entity& entity) {
			std::vector<MaterialSlotInfo> slots{};
			if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
				// 未指定のMaterialは描画時と同じ既定値を使う
				const Engine::AssetID material =
					renderer->material ? renderer->material : Engine::DefaultMaterialSettings::GetInstance().GetMeshOrBuiltin();
				const std::span<const Engine::SubMeshMaterial> subMeshes = Engine::GetMeshSubMeshes(world, entity);
				for (size_t i = 0; i < subMeshes.size(); ++i) {
					MaterialSlotInfo slot{};
					slot.pathPrefix = std::format("subMeshes[{}]", i);
					// 表示はサブメッシュ名、未設定ならpathPrefixをそのまま使う
					slot.displayPrefix = subMeshes[i].name.empty() ? slot.pathPrefix : subMeshes[i].name;
					slot.material = material;
					slots.emplace_back(std::move(slot));
				}
			}
			return slots;
		};
		provider.locateOverrides = [](Engine::ECSWorld& world, const Engine::Entity& entity,
									   std::string_view prefix) -> OverridesMap* {
			uint32_t index = 0;
			if (!ParseSubMeshIndex(prefix, index)) {
				return nullptr;
			}
			if (!world.HasComponent<Engine::MeshRendererComponent>(entity)) {
				return nullptr;
			}
			const std::span<Engine::SubMeshMaterial> subMeshes = Engine::GetMeshSubMeshes(world, entity);
			if (index < subMeshes.size()) {
				return &subMeshes[index].materialInstance;
			}
			return nullptr;
		};
		return provider;
	}

	// 単一MaterialのRendererを登録する
	template <typename Component>
	MaterialSlotProvider MakeSingleSlotProvider(std::string componentName, Engine::AssetID (*resolveDefaultMaterial)()) {

		MaterialSlotProvider provider{};
		provider.componentName = std::move(componentName);
		provider.cbufferName = Engine::MaterialParameterCBuffer::kSurface;
		provider.enumerateSlots = [resolveDefaultMaterial](Engine::ECSWorld& world, const Engine::Entity& entity) {
			std::vector<MaterialSlotInfo> slots{};
			if (Component* renderer = world.TryGetComponent<Component>(entity)) {
				const Engine::AssetID material = renderer->material ? renderer->material : resolveDefaultMaterial();
				MaterialSlotInfo slot{};
				slot.material = material;
				slots.emplace_back(std::move(slot));
			}
			return slots;
		};
		provider.locateOverrides = [](Engine::ECSWorld& world, const Engine::Entity& entity,
									   std::string_view prefix) -> OverridesMap* {
			// 単一slotなのでprefixは空文字のみ受け付ける
			if (!prefix.empty()) {
				return nullptr;
			}
			if (Component* renderer = world.TryGetComponent<Component>(entity)) {
				return &renderer->materialInstance;
			}
			return nullptr;
		};
		return provider;
	}

	// 全SubMeshを一括で変更する参照を作る
	Engine::AnimationPropertyDescriptor MakeAllMeshParamDescriptor(
		std::string propertyPath, std::string displayName, Engine::AnimationValueType valueType, std::string paramName) {

		Engine::AnimationPropertyDescriptor desc{};
		desc.componentName = "MeshRenderer";
		desc.propertyPath = std::move(propertyPath);
		desc.displayName = std::move(displayName);
		desc.valueType = valueType;
		desc.hasComponent = [](Engine::ECSWorld& world, const Engine::Entity& entity) {
			return world.HasComponent<Engine::MeshRendererComponent>(entity);
		};
		desc.snapshotBindings = [paramName, valueType](Engine::ECSWorld& world, const Engine::Entity& entity) {
			std::vector<Engine::AnimationPropertyBinding> bindings;
			const auto subMeshes = Engine::GetMeshSubMeshes(world, entity);
			bindings.reserve(subMeshes.size());
			for (size_t index = 0; index < subMeshes.size(); ++index) {

				bindings.push_back({"MeshRenderer", std::format("subMeshes[{}].material.{}", index, paramName), valueType});
			}
			return bindings;
		};
		desc.getValue = [paramName, valueType](
							Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {
			const std::span<Engine::SubMeshMaterial> subMeshes = Engine::GetMeshSubMeshes(world, entity);
			if (subMeshes.empty()) {
				return false;
			}
			// 先頭サブメッシュを代表値とし、未設定なら0を現在値として返す
			const auto& map = subMeshes[0].materialInstance;
			auto it = map.find(paramName);
			if (it != map.end() && Engine::MaterialValueToAnimation(it->second, valueType, out)) {
				return true;
			}
			out = Engine::ZeroAnimationValue(valueType);
			return true;
		};
		desc.setValue = [paramName](Engine::ECSWorld& world, const Engine::Entity& entity,
							const Engine::AnimationPropertyValue& value) {
			if (!world.HasComponent<Engine::MeshRendererComponent>(entity)) {
				return false;
			}
			const Engine::MaterialParameterValue materialValue = Engine::AnimationValueToMaterial(value);
			for (Engine::SubMeshMaterial& subMesh : Engine::GetMeshSubMeshes(world, entity)) {
				subMesh.materialInstance.Set(paramName, materialValue);
			}
			world.MarkComponentModified<Engine::MeshRendererComponent>(entity);
			return true;
		};
		desc.hasValue = [paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {
			const std::span<Engine::SubMeshMaterial> subMeshes = Engine::GetMeshSubMeshes(world, entity);
			return !subMeshes.empty() && subMeshes[0].materialInstance.find(paramName) != subMeshes[0].materialInstance.end();
		};
		desc.clearValue = [paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {
			if (!world.HasComponent<Engine::MeshRendererComponent>(entity)) {
				return false;
			}
			bool removed = false;
			for (Engine::SubMeshMaterial& subMesh : Engine::GetMeshSubMeshes(world, entity)) {
				removed |= subMesh.materialInstance.erase(paramName) != 0;
			}
			if (removed) {
				world.MarkComponentModified<Engine::MeshRendererComponent>(entity);
			}
			return true;
		};
		return desc;
	}

	// 全SubMesh共通のMaterialパラメータを列挙する
	void EnumerateAllMeshProperties(const MaterialSlotProvider& provider, const Engine::AnimationPropertyQueryContext& context,
		Engine::ECSWorld& world, const Engine::Entity& entity, std::vector<Engine::AnimationPropertyDescriptor>& out) {

		const std::vector<MaterialSlotInfo> slots = provider.enumerateSlots(world, entity);
		if (slots.empty()) {
			return;
		}
		const auto params = Engine::CollectMaterialAnimationParameters(context, slots.front().material, provider.cbufferName);
		for (const auto& [name, type] : params) {
			out.emplace_back(MakeAllMeshParamDescriptor(std::format("allMesh.material.{}", name),
				MakeMaterialParamDisplay(provider.componentName, "全メッシュ", name), type, name));
		}
	}

	// MeshRendererの一括編集と個別参照を登録する
	void RegisterMeshAnimationAccessor(Engine::AnimationPropertyRegistry& registry) {

		MaterialSlotProvider provider = MakeMeshProvider();
		Engine::MaterialAnimationAccessor accessor{};
		accessor.componentName = provider.componentName;
		accessor.enumerate = [provider](const Engine::AnimationPropertyQueryContext& context, Engine::ECSWorld& world,
								 const Engine::Entity& entity, std::vector<Engine::AnimationPropertyDescriptor>& out) {
			// 追加メニューには一括編集を表示する
			EnumerateAllMeshProperties(provider, context, world, entity, out);
		};
		accessor.resolve = [provider](Engine::ECSWorld&, const Engine::Entity&, std::string_view propertyPath,
							   Engine::AnimationValueType valueType) -> std::optional<Engine::AnimationPropertyDescriptor> {
			std::string prefix{};
			std::string name{};
			if (SplitMaterialParamPath(propertyPath, prefix, name) && prefix == "allMesh") {
				return MakeAllMeshParamDescriptor(std::string(propertyPath),
					MakeMaterialParamDisplay(provider.componentName, "全メッシュ", name), valueType, name);
			}
			return ResolveMaterialProperty(provider, propertyPath, valueType);
		};
		registry.RegisterMaterialAccessor(accessor);
	}
}

//============================================================================
//	RegisterMaterialAnimationAccessors
//============================================================================
void Engine::RegisterMaterialAnimationAccessors() {

	// 重複登録を防ぐ
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	AnimationPropertyRegistry& registry = AnimationPropertyRegistry::GetInstance();

	RegisterMeshAnimationAccessor(registry);
	RegisterMaterialSlotProvider(registry, MakeSingleSlotProvider<SpriteRendererComponent>("SpriteRenderer",
											   []() { return DefaultMaterialSettings::GetInstance().GetSpriteOrBuiltin(); }));
	RegisterMaterialSlotProvider(registry, MakeSingleSlotProvider<TextRendererComponent>("TextRenderer",
											   []() { return DefaultMaterialSettings::GetInstance().GetTextOrBuiltin(); }));
}
