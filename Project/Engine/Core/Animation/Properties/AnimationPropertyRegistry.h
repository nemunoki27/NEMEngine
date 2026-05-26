#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;

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

	// Componentのどの値をAnimationClipから触れるかを表す。
	// get/setを関数ポインタにして、Editor/Runtimeのどちらからも使えるようにする。
	struct AnimationPropertyDescriptor {

		std::string componentName;
		std::string propertyPath;
		std::string displayName;
		AnimationValueType valueType = AnimationValueType::Float;

		bool (*hasComponent)(ECSWorld& world, const Entity& entity) = nullptr;
		bool (*getValue)(ECSWorld& world, const Entity& entity, AnimationPropertyValue& out) = nullptr;
		bool (*setValue)(ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value) = nullptr;
	};

	//============================================================================
	//	AnimationPropertyRegistry class
	//============================================================================
	class AnimationPropertyRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		static AnimationPropertyRegistry& GetInstance();

		void Register(const AnimationPropertyDescriptor& desc);
		const AnimationPropertyDescriptor* Find(std::string_view componentName, std::string_view propertyPath) const;
		std::vector<const AnimationPropertyDescriptor*> GetPropertiesForEntity(ECSWorld& world, const Entity& entity) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		std::vector<AnimationPropertyDescriptor> properties_;
	};

	// Builtin Componentのアニメーション可能プロパティを登録する。
	// Tool起動前に何度呼ばれても重複登録されないように実装側でガードする。
	void RegisterBuiltinAnimationProperties();
} // Engine
