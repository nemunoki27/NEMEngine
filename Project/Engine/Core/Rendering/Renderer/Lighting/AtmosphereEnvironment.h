#pragma once

//============================================================================
//	include
//============================================================================
#include "SceneSkyboxResolver.h"
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <memory>
#include <string_view>

namespace Engine {

	struct AtmosphereViewData {

		uint32_t aerialIndex = UINT32_MAX;
		float maxDistance = 32000.0f;
		float strength = 1.0f;
		Vector3 sunDirection = Vector3(0.0f, -1.0f, 0.0f);
		bool sunAvailable = false;
		uint32_t skyIndex = UINT32_MAX;
		Vector3 solarLuminance = Vector3::AnyInit(0.0f);
		float sunAngularRadius = 0.004712389f;
	};

	//============================================================================
	//	AtmosphereEnvironment class
	//	大気の積分表と空からの環境光を管理する
	//============================================================================
	class AtmosphereEnvironment {
	public:
		AtmosphereEnvironment();
		~AtmosphereEnvironment();

		SceneSkyboxInfo Resolve(GraphicsCore& graphics, ECSWorld& world, uint32_t cullingMask);
		AtmosphereViewData PrepareView(GraphicsCore& graphics, ECSWorld& world, uint32_t cullingMask,
			std::string_view viewKey, const Vector3& cameraPosition, const Matrix4x4& inverseViewProjection);
		void CollectUnused();
	private:
		struct State;
		std::unique_ptr<State> state_;
	};
}
