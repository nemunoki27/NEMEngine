#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Asset/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	SkinnedAnimationComponent struct
	//============================================================================

	struct SkinnedAnimationComponent {

		// アニメーションの有効/無効
		bool enabled = true;
		// ループ再生するか
		bool loop = true;
		bool playInEditMode = true;

		// 再生速度
		float playbackSpeed = 1.0f;

		// アニメーションの遷移時間
		float transitionDuration = 0.15f;

		// 再生するアニメーションクリップの名前
		std::string clip = "Default";

		// アニメーション対象のメッシュ
		AssetID runtimeMesh{};
		bool runtimeInitialized = false;

		// 再生中のアニメーションクリップの名前
		std::string runtimeCurrentClip{};
		// 遷移中のアニメーションクリップの名前
		std::string runtimeFromClip{};
		std::string runtimeToClip{};

		// 再生時間
		float runtimeTime = 0.0f;
		// 遷移時間
		float runtimeFromTime = 0.0f;
		float runtimeNextTime = 0.0f;
		float runtimeBlendTime = 0.0f;

		// 遷移中か
		bool runtimeInTransition = false;
		// アニメーションが終了したか
		bool runtimeAnimationFinished = false;
		// ループ再生した回数
		int32_t runtimeRepeatCount = 0;

		// スケルトンのランタイムデータ
		Skeleton runtimeBindSkeleton{};
		Skeleton runtimeSkeleton{};
		std::vector<WellForGPU> palette{};

		// 再生可能なアニメーションクリップの名前
		std::vector<std::string> runtimeAvailableClips{};
	};

	// json変換
	void from_json(const nlohmann::json& in, SkinnedAnimationComponent& component);
	void to_json(nlohmann::json& out, const SkinnedAnimationComponent& component);

	ENGINE_REGISTER_COMPONENT(SkinnedAnimationComponent, "SkinnedAnimation");
} // Engine