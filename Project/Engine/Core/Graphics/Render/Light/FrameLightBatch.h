#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Color.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	FrameLightBatch structures
	//============================================================================

	// ライト共通情報
	struct LightItemCommon {

		// 所有エンティティ
		Entity entity{};
		ECSWorld* world = nullptr;

		// シーンインスタンス
		UUID sceneInstanceID{};

		// どのレイヤーに影響するか
		uint32_t affectLayerMask = 0xFFFFFFFFu;

		// どのカメラドメイン向けか
		RenderCameraDomain cameraDomain = RenderCameraDomain::Perspective;
	};
	// 平行光源
	struct DirectionalLightItem {

		LightItemCommon common{};

		Color4 color = Color4::White();
		Vector3 direction = Vector3(0.0f, -1.0f, 0.0f);

		float intensity = 1.0f;
	};
	// 点光源
	struct PointLightItem {

		LightItemCommon common{};

		Color4 color = Color4::White();
		Vector3 pos = Vector3::AnyInit(0.0f);

		float intensity = 1.0f;
		float radius = 8.0f;
		float decay = 1.0f;
	};
	// スポット光源
	struct SpotLightItem {

		LightItemCommon common{};

		Color4 color = Color4::White();
		Vector3 pos = Vector3::AnyInit(0.0f);
		Vector3 direction = Vector3(0.0f, 0.0f, 1.0f);

		float intensity = 1.0f;
		float distance = 10.0f;
		float decay = 1.0f;
		float cosAngle = 0.0f;
		float cosFalloffStart = 1.0f;
	};
	// 描画に使用するライトのセット
	struct PerViewLightSet {

		const ResolvedRenderView* view = nullptr;
		const ResolvedCameraView* camera = nullptr;

		std::vector<const DirectionalLightItem*> directionalLights;
		std::vector<const PointLightItem*> pointLights;
		std::vector<const SpotLightItem*> spotLights;

		// データクリア
		void Clear();

		// ライトが一つもないか
		bool IsEmpty() const;

		// ライトの数を返す
		uint32_t GetDirectionalCount() const;
		uint32_t GetPointCount() const;
		uint32_t GetSpotCount() const;
		uint32_t GetLocalLightCount() const;
		uint32_t GetTotalCount() const;
	};

	//============================================================================
	//	FrameLightBatch class
	//	1フレームのライトのバッチ処理を行うクラス
	//============================================================================
	class FrameLightBatch {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FrameLightBatch() = default;
		~FrameLightBatch() = default;

		// ライト追加
		void Add(DirectionalLightItem&& item);
		void Add(PointLightItem&& item);
		void Add(SpotLightItem&& item);

		// データクリア
		void Clear();

		// ソート
		void Sort();

		//--------- accessor -----------------------------------------------------

		// ライトのセットを返す
		const std::vector<DirectionalLightItem>& GetDirectionalLights() const { return directionalLights_; }
		const std::vector<PointLightItem>& GetPointLights() const { return pointLights_; }
		const std::vector<SpotLightItem>& GetSpotLights() const { return spotLights_; }

		// ライトの数を返す
		uint32_t GetDirectionalCount() const { return static_cast<uint32_t>(directionalLights_.size()); }
		uint32_t GetPointCount() const { return static_cast<uint32_t>(pointLights_.size()); }
		uint32_t GetSpotCount() const { return static_cast<uint32_t>(spotLights_.size()); }
		uint32_t GetTotalCount() const { return GetDirectionalCount() + GetPointCount() + GetSpotCount(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<DirectionalLightItem> directionalLights_{};
		std::vector<PointLightItem> pointLights_{};
		std::vector<SpotLightItem> spotLights_{};
	};
} // Engine