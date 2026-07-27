#include "AnimationPropertyRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>
#include <format>
#include <unordered_map>
#include <variant>

//============================================================================
//	MaterialAnimationProperties internal
//============================================================================
namespace {

	using OverridesMap = std::unordered_map<std::string, Engine::MaterialParameterValue>;
	using OverridesLocator = std::function<OverridesMap* (Engine::ECSWorld&, const Engine::Entity&)>;

	// 成分数で値型を決める、色判定はreflectionに無いのでシェーダー側メタデータのisColorに従う
	Engine::AnimationValueType ResolveAnimationValueType(const Engine::ShaderConstantBufferVariable& var) {

		const uint32_t count = Engine::GetVariableComponentCount(var);
		switch (count) {
		case 1:  return Engine::AnimationValueType::Float;
		case 2:  return Engine::AnimationValueType::Vector2;
		case 3:  return var.isColor ? Engine::AnimationValueType::Color3 : Engine::AnimationValueType::Vector3;
		default: return var.isColor ? Engine::AnimationValueType::Color4 : Engine::AnimationValueType::Vector4;
		}
	}

	// cbufferからfloat系paramを集める、テクスチャは型uintで除外しpaddingは使用フラグで除外する
	void CollectFloatFamilyParams(const Engine::ShaderReflectionInfo& reflection, const std::string& cbufferName,
		std::vector<std::pair<std::string, Engine::AnimationValueType>>& out) {

		for (const Engine::ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			if (cb.name != cbufferName) {
				continue;
			}
			for (const Engine::ShaderConstantBufferVariable& var : cb.variables) {

				if (var.valueType != D3D_SVT_FLOAT || !var.used) {
					continue;
				}
				out.emplace_back(var.name, ResolveAnimationValueType(var));
			}
		}
	}

	// マテリアルのDrawパスreflectionを引く、editor列挙でのみ使うのでreflection前提
	const Engine::ShaderReflectionInfo* GetMaterialDrawReflection(
		const Engine::AnimationPropertyQueryContext& context, Engine::AssetID materialID) {

		if (!context.assetDatabase || !context.renderPipeline || !materialID) {
			return nullptr;
		}
		const std::filesystem::path path = context.assetDatabase->ResolveFullPath(materialID);
		if (path.empty()) {
			return nullptr;
		}
		nlohmann::json data = Engine::JsonAdapter::Load(path, false);
		Engine::MaterialAsset material{};
		if (!Engine::FromJson(data, material)) {
			return nullptr;
		}
		return context.renderPipeline->FindMaterialDrawReflection(material);
	}

	// 型に対応する0値、override未設定時の現在値プレースホルダに使う
	Engine::AnimationPropertyValue ZeroAnimationValue(Engine::AnimationValueType type) {

		switch (type) {
		case Engine::AnimationValueType::Vector2: return Engine::Vector2{};
		case Engine::AnimationValueType::Vector3: return Engine::Vector3{};
		case Engine::AnimationValueType::Vector4: return Engine::Vector4{};
		case Engine::AnimationValueType::Color3:  return Engine::Color3{};
		case Engine::AnimationValueType::Color4:  return Engine::Color4{};
		default:                                  return 0.0f;
		}
	}

	// MaterialParameterValueをアニメーション値へ変換する、3成分カラーはVector3で保持される
	bool MaterialValueToAnimation(const Engine::MaterialParameterValue& value,
		Engine::AnimationValueType type, Engine::AnimationPropertyValue& out) {

		switch (type) {
		case Engine::AnimationValueType::Float:
			if (const float* v = std::get_if<float>(&value.value)) { out = *v; return true; }
			break;
		case Engine::AnimationValueType::Vector2:
			if (const Engine::Vector2* v = std::get_if<Engine::Vector2>(&value.value)) { out = *v; return true; }
			break;
		case Engine::AnimationValueType::Vector3:
			if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value.value)) { out = *v; return true; }
			break;
		case Engine::AnimationValueType::Vector4:
			if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value.value)) { out = *v; return true; }
			break;
		case Engine::AnimationValueType::Color3:
			if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value.value)) { out = Engine::Color3(v->x, v->y, v->z); return true; }
			if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value.value)) { out = Engine::Color3(v->r, v->g, v->b); return true; }
			break;
		case Engine::AnimationValueType::Color4:
			if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value.value)) { out = *v; return true; }
			if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value.value)) { out = Engine::Color4(v->x, v->y, v->z, v->w); return true; }
			break;
		default:
			break;
		}
		return false;
	}

	// アニメーション値をparameterOverridesへ書き込むMaterialParameterValueへ変換する
	Engine::MaterialParameterValue AnimationValueToMaterial(const Engine::AnimationPropertyValue& value) {

		Engine::MaterialParameterValue result{};
		if (const float* v = std::get_if<float>(&value)) { result.value = *v; return result; }
		if (const Engine::Vector2* v = std::get_if<Engine::Vector2>(&value)) { result.value = *v; return result; }
		if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value)) { result.value = *v; return result; }
		if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value)) { result.value = *v; return result; }
		// 3成分カラーはMaterialParameterValueにColor3が無いのでVector3として保持する
		if (const Engine::Color3* v = std::get_if<Engine::Color3>(&value)) { result.value = Engine::Vector3(v->r, v->g, v->b); return result; }
		if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value)) { result.value = *v; return result; }
		return result;
	}

	// "subMeshes[N].material.Name" や "material.Name" を prefix と paramName に分ける
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

	// locatorとparam名から1プロパティ分のget/set/hasを組み立てる
	Engine::AnimationPropertyDescriptor MakeMaterialParamDescriptor(std::string componentName,
		std::string propertyPath, std::string displayName, Engine::AnimationValueType valueType,
		std::string paramName, OverridesLocator locator) {

		Engine::AnimationPropertyDescriptor desc{};
		desc.componentName = std::move(componentName);
		desc.propertyPath = std::move(propertyPath);
		desc.displayName = std::move(displayName);
		desc.valueType = valueType;
		desc.hasComponent = [locator](Engine::ECSWorld& world, const Engine::Entity& entity) {
			return locator(world, entity) != nullptr;
			};
		desc.getValue = [locator, paramName, valueType](Engine::ECSWorld& world, const Engine::Entity& entity,
			Engine::AnimationPropertyValue& out) {

				OverridesMap* map = locator(world, entity);
				if (!map) {
					return false;
				}
				auto it = map->find(paramName);
				if (it != map->end() && MaterialValueToAnimation(it->second, valueType, out)) {
					return true;
				}
				// override未設定なら0値を現在値として返し、プロパティ追加を許可する
				out = ZeroAnimationValue(valueType);
				return true;
			};
		desc.setValue = [locator, paramName](Engine::ECSWorld& world, const Engine::Entity& entity,
			const Engine::AnimationPropertyValue& value) {

				OverridesMap* map = locator(world, entity);
				if (!map) {
					return false;
				}
				(*map)[paramName] = AnimationValueToMaterial(value);
				return true;
			};
		// override未設定のparamはPreview前に値が無いので、復元時はsetでなく除去して既定の見た目へ戻す
		desc.hasValue = [locator, paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {

				OverridesMap* map = locator(world, entity);
				return map && map->find(paramName) != map->end();
			};
		desc.clearValue = [locator, paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {

				OverridesMap* map = locator(world, entity);
				if (!map) {
					return false;
				}
				map->erase(paramName);
				return true;
			};
		return desc;
	}

	// 1slot分の情報、pathPrefixはpropertyPath用、displayPrefixは表示用でサブメッシュ名などを入れる
	struct MaterialSlotInfo {

		std::string pathPrefix;
		std::string displayPrefix;
		Engine::AssetID material{};
	};

	//============================================================================
	//	汎用Material slotプロバイダ、Component別にslot列挙とoverrides解決だけ差し替える
	//============================================================================
	struct MaterialSlotProvider {

		std::string componentName;
		// reflectionで参照するマテリアルパラメータcbuffer名
		std::string cbufferName;
		// editor列挙、各slotのpath/表示prefixと実効マテリアルAssetIDを返す
		std::function<std::vector<MaterialSlotInfo>(Engine::ECSWorld&, const Engine::Entity&)> enumerateSlots;
		// runtime、pathPrefixからoverrides mapを引く
		std::function<OverridesMap* (Engine::ECSWorld&, const Engine::Entity&, std::string_view)> locateOverrides;
	};

	// pathPrefixとparam名からpropertyPathを作る、単一slotはprefix無しのmaterial.Nameになる
	std::string MakeMaterialParamPath(const std::string& prefix, const std::string& name) {

		return prefix.empty() ? std::format("material.{}", name) : std::format("{}.material.{}", prefix, name);
	}
	std::string MakeMaterialParamDisplay(const std::string& componentName, const std::string& prefix, const std::string& name) {

		return prefix.empty() ? std::format("{}.{}", componentName, name) :
			std::format("{}.{}.{}", componentName, prefix, name);
	}

	// providerのslotごとにreflectionからfloat系パラメータを列挙してdescriptorを作る
	void EnumerateMaterialProperties(const MaterialSlotProvider& provider,
		const Engine::AnimationPropertyQueryContext& context, Engine::ECSWorld& world, const Engine::Entity& entity,
		std::vector<Engine::AnimationPropertyDescriptor>& out) {

		for (const MaterialSlotInfo& slot : provider.enumerateSlots(world, entity)) {

			const Engine::ShaderReflectionInfo* reflection = GetMaterialDrawReflection(context, slot.material);
			if (!reflection) {
				continue;
			}
			std::vector<std::pair<std::string, Engine::AnimationValueType>> params{};
			CollectFloatFamilyParams(*reflection, provider.cbufferName, params);

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

	// propertyPathから単一プロパティのget/setを組み立てる、reflection不要なのでRuntimeでも使える
	std::optional<Engine::AnimationPropertyDescriptor> ResolveMaterialProperty(const MaterialSlotProvider& provider,
		std::string_view propertyPath, Engine::AnimationValueType valueType) {

		std::string prefix{};
		std::string name{};
		if (!SplitMaterialParamPath(propertyPath, prefix, name)) {
			return std::nullopt;
		}
		const std::string componentName = provider.componentName;
		const auto& locateOverrides = provider.locateOverrides;
		return MakeMaterialParamDescriptor(componentName, std::string(propertyPath),
			MakeMaterialParamDisplay(componentName, prefix, name), valueType, name,
			[locateOverrides, prefix](Engine::ECSWorld& world, const Engine::Entity& entity) {
				return locateOverrides(world, entity, prefix);
			});
	}

	// providerからMaterialAnimationAccessorを組み立てて登録する
	void RegisterMaterialSlotProvider(Engine::AnimationPropertyRegistry& registry, MaterialSlotProvider provider) {

		Engine::MaterialAnimationAccessor accessor{};
		accessor.componentName = provider.componentName;
		accessor.enumerate = [provider](const Engine::AnimationPropertyQueryContext& context, Engine::ECSWorld& world,
			const Engine::Entity& entity, std::vector<Engine::AnimationPropertyDescriptor>& out) {
				EnumerateMaterialProperties(provider, context, world, entity, out);
			};
		accessor.resolve = [provider](Engine::ECSWorld&, const Engine::Entity&,
			std::string_view propertyPath, Engine::AnimationValueType valueType) {
				return ResolveMaterialProperty(provider, propertyPath, valueType);
			};
		registry.RegisterMaterialAccessor(accessor);
	}

	//============================================================================
	//	Component別プロバイダ
	//============================================================================
	// MeshRendererはsubMeshes[N]ごとに別々のparameterOverridesを持つ
	MaterialSlotProvider MakeMeshProvider() {

		MaterialSlotProvider provider{};
		provider.componentName = "MeshRenderer";
		provider.cbufferName = Engine::MaterialParameterCBuffer::kMesh;
		provider.enumerateSlots = [](Engine::ECSWorld& world, const Engine::Entity& entity) {

			std::vector<MaterialSlotInfo> slots{};
			if (Engine::MeshRendererComponent* renderer = world.TryGetComponent<Engine::MeshRendererComponent>(entity)) {
				// 空マテリアルは描画時に既定へ解決されるので、reflectionも実効デフォルトから引く
				const Engine::AssetID material = renderer->material ?
					renderer->material : Engine::DefaultMaterialSettings::GetInstance().GetMeshOrBuiltin();
				const std::span<const Engine::SubMeshMaterial> subMeshes =
					Engine::GetMeshSubMeshes(world, entity);
				for (size_t i = 0; i < subMeshes.size(); ++i) {
					MaterialSlotInfo slot{};
					slot.pathPrefix = std::format("subMeshes[{}]", i);
					// 表示はサブメッシュ名、未設定ならpathPrefixをそのまま使う
					slot.displayPrefix =
						subMeshes[i].name.empty() ? slot.pathPrefix : subMeshes[i].name;
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
				const std::span<Engine::SubMeshMaterial> subMeshes =
					Engine::GetMeshSubMeshes(world, entity);
				if (index < subMeshes.size()) {
					return &subMeshes[index].parameterOverrides.GetMutable();
				}
				return nullptr;
			};
		return provider;
	}

	// Sprite/Textは単一マテリアルなのでslotは1つ、pathPrefixは空文字で扱う
	template <typename Component>
	MaterialSlotProvider MakeSingleSlotProvider(std::string componentName, Engine::AssetID(*resolveDefaultMaterial)()) {

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
					return &renderer->parameterOverrides.GetMutable();
				}
				return nullptr;
			};
		return provider;
	}

	// 全サブメッシュへ同じparamを設定するdescriptor、get/hasは先頭サブメッシュ、set/clearは全サブメッシュへ反映する
	Engine::AnimationPropertyDescriptor MakeAllMeshParamDescriptor(std::string propertyPath,
		std::string displayName, Engine::AnimationValueType valueType, std::string paramName) {

		Engine::AnimationPropertyDescriptor desc{};
		desc.componentName = "MeshRenderer";
		desc.propertyPath = std::move(propertyPath);
		desc.displayName = std::move(displayName);
		desc.valueType = valueType;
		desc.hasComponent = [](Engine::ECSWorld& world, const Engine::Entity& entity) {
			return world.HasComponent<Engine::MeshRendererComponent>(entity);
			};
		desc.getValue = [paramName, valueType](Engine::ECSWorld& world, const Engine::Entity& entity,
			Engine::AnimationPropertyValue& out) {

				const std::span<Engine::SubMeshMaterial> subMeshes =
					Engine::GetMeshSubMeshes(world, entity);
				if (subMeshes.empty()) {
					return false;
				}
				// 先頭サブメッシュを代表値とし、未設定なら0を現在値として返す
				const auto& map = subMeshes[0].parameterOverrides;
				auto it = map.find(paramName);
				if (it != map.end() && MaterialValueToAnimation(it->second, valueType, out)) {
					return true;
				}
				out = ZeroAnimationValue(valueType);
				return true;
			};
		desc.setValue = [paramName](Engine::ECSWorld& world, const Engine::Entity& entity,
			const Engine::AnimationPropertyValue& value) {

				if (!world.HasComponent<Engine::MeshRendererComponent>(entity)) {
					return false;
				}
				const Engine::MaterialParameterValue materialValue = AnimationValueToMaterial(value);
				for (Engine::SubMeshMaterial& subMesh :
					Engine::GetMeshSubMeshes(world, entity)) {
					subMesh.parameterOverrides[paramName] = materialValue;
				}
				return true;
			};
		desc.hasValue = [paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {
			const std::span<Engine::SubMeshMaterial> subMeshes =
				Engine::GetMeshSubMeshes(world, entity);
			return !subMeshes.empty() &&
				subMeshes[0].parameterOverrides.find(paramName) !=
				subMeshes[0].parameterOverrides.end();
			};
		desc.clearValue = [paramName](Engine::ECSWorld& world, const Engine::Entity& entity) {
			if (!world.HasComponent<Engine::MeshRendererComponent>(entity)) {
				return false;
			}
			for (Engine::SubMeshMaterial& subMesh :
				Engine::GetMeshSubMeshes(world, entity)) {
				subMesh.parameterOverrides.erase(paramName);
			}
			return true;
			};
		return desc;
	}

	// 全サブメッシュ共通のparamを列挙する、全slotは同じマテリアルを参照するので先頭のreflectionを使う
	void EnumerateAllMeshProperties(const MaterialSlotProvider& provider,
		const Engine::AnimationPropertyQueryContext& context, Engine::ECSWorld& world, const Engine::Entity& entity,
		std::vector<Engine::AnimationPropertyDescriptor>& out) {

		const std::vector<MaterialSlotInfo> slots = provider.enumerateSlots(world, entity);
		if (slots.empty()) {
			return;
		}
		const Engine::ShaderReflectionInfo* reflection = GetMaterialDrawReflection(context, slots.front().material);
		if (!reflection) {
			return;
		}
		std::vector<std::pair<std::string, Engine::AnimationValueType>> params{};
		CollectFloatFamilyParams(*reflection, provider.cbufferName, params);
		for (const auto& [name, type] : params) {
			out.emplace_back(MakeAllMeshParamDescriptor(std::format("allMesh.material.{}", name),
				MakeMaterialParamDisplay(provider.componentName, "全メッシュ", name), type, name));
		}
	}

	// MeshRenderer用のアクセサ、AllMeshを先頭に列挙しサブメッシュ個別と併せて公開する
	void RegisterMeshAnimationAccessor(Engine::AnimationPropertyRegistry& registry) {

		MaterialSlotProvider provider = MakeMeshProvider();
		Engine::MaterialAnimationAccessor accessor{};
		accessor.componentName = provider.componentName;
		accessor.enumerate = [provider](const Engine::AnimationPropertyQueryContext& context, Engine::ECSWorld& world,
			const Engine::Entity& entity, std::vector<Engine::AnimationPropertyDescriptor>& out) {

				// 追加メニューには全メッシュのみ出す、subMeshes[N]個別は出さない
				EnumerateAllMeshProperties(provider, context, world, entity, out);
			};
		accessor.resolve = [provider](Engine::ECSWorld&, const Engine::Entity&,
			std::string_view propertyPath, Engine::AnimationValueType valueType)
			-> std::optional<Engine::AnimationPropertyDescriptor> {

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

	// Builtinと同じく複数回呼ばれても重複登録しない
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	AnimationPropertyRegistry& registry = AnimationPropertyRegistry::GetInstance();

	RegisterMeshAnimationAccessor(registry);
	RegisterMaterialSlotProvider(registry, MakeSingleSlotProvider<SpriteRendererComponent>(
		"SpriteRenderer", []() { return DefaultMaterialSettings::GetInstance().GetSpriteOrBuiltin(); }));
	RegisterMaterialSlotProvider(registry, MakeSingleSlotProvider<TextRendererComponent>(
		"TextRenderer", []() { return DefaultMaterialSettings::GetInstance().GetTextOrBuiltin(); }));
}
