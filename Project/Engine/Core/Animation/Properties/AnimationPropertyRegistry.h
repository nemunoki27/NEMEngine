#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;
	class AssetDatabase;
	class RenderPipelineRunner;

	//============================================================================
	//	AnimationPropertyRegistry structures
	//============================================================================
	using AnimationPropertyValue = std::variant<
		float,
		Vector2,
		Vector3,
		Vector4,
		Color3,
		Color4,
		Quaternion>;

	// Componentのアニメーション可能な1プロパティ、get/setはstd::functionで静的にも動的にも使える
	struct AnimationPropertyDescriptor {

		std::string componentName;
		std::string propertyPath;
		std::string displayName;
		AnimationValueType valueType = AnimationValueType::Float;

		std::function<bool(ECSWorld& world, const Entity& entity)> hasComponent;
		std::function<bool(ECSWorld& world, const Entity& entity, AnimationPropertyValue& out)> getValue;
		std::function<bool(ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value)> setValue;
	};

	// 動的プロパティ列挙時に渡すreflectionアクセス手段、Runtime解決には不要で空でよい
	struct AnimationPropertyQueryContext {

		AssetDatabase* assetDatabase = nullptr;
		RenderPipelineRunner* renderPipeline = nullptr;
	};

	// 個別マテリアルパラメータ用のComponent別アクセサ、enumerateで列挙しresolveで単一解決する
	struct MaterialAnimationAccessor {

		std::string componentName;
		std::function<void(const AnimationPropertyQueryContext& context, ECSWorld& world, const Entity& entity,
			std::vector<AnimationPropertyDescriptor>& outProperties)> enumerate;
		std::function<std::optional<AnimationPropertyDescriptor>(ECSWorld& world, const Entity& entity,
			std::string_view propertyPath, AnimationValueType valueType)> resolve;
	};

	//============================================================================
	//	AnimationPropertyRegistry class
	//============================================================================
	class AnimationPropertyRegistry {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		static AnimationPropertyRegistry& GetInstance();

		void Register(const AnimationPropertyDescriptor& desc);
		void RegisterMaterialAccessor(const MaterialAnimationAccessor& accessor);

		// 静的に登録済みのプロパティを名前とパスで引く、動的プロパティは含まない
		const AnimationPropertyDescriptor* Find(std::string_view componentName, std::string_view propertyPath) const;

		// Editor用、エンティティが持つ全アニメーション可能プロパティを静的と動的の両方返す
		std::vector<AnimationPropertyDescriptor> CollectProperties(ECSWorld& world, const Entity& entity,
			const AnimationPropertyQueryContext& context) const;

		// 単一bindingを解決する、静的に無ければマテリアルアクセサへフォールバックしreflection不要
		std::optional<AnimationPropertyDescriptor> ResolveProperty(ECSWorld& world, const Entity& entity,
			std::string_view componentName, std::string_view propertyPath, AnimationValueType valueType) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================
		std::vector<AnimationPropertyDescriptor> properties_;
		std::vector<MaterialAnimationAccessor> materialAccessors_;
	};

	// Builtin Componentのアニメーション可能プロパティを登録する
	// Tool起動前に何度呼ばれても重複登録されないように実装側でガードする
	void RegisterBuiltinAnimationProperties();

	// 個別マテリアルパラメータ用のMaterialAnimationAccessorを登録する
	void RegisterMaterialAnimationAccessors();
} // Engine
