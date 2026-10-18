#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>

namespace Engine {

	//============================================================================
	//	DecalProjectorComponent structure
	//	箱の範囲へ表面情報を投影するコンポーネント
	//============================================================================
	struct DecalProjectorComponent {

		// 投影だけの変更でMeshやTLASを再構築しない
		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::None;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::None;

		AssetID material{};
		MaterialInstanceParameters materialInstance{};
		bool enabled = true;
		// TransformのScaleを掛けるローカル寸法
		Vector3 size{1.0f, 1.0f, 0.1f};
		Vector3 pivot{};
		Color4 color = Color4::White();
		float fadeFactor = 1.0f;
		Vector2 uvScale{1.0f, 1.0f};
		Vector2 uvBias{};
		float uvRotation = 0.0f;
		int32_t sortOrder = 0;
		float drawDistance = 1000.0f;
		float fadeScale = 0.9f;
		float startAngleFade = 60.0f;
		float endAngleFade = 85.0f;
		// Cameraへの表示と受け取り先の選別を分ける
		uint32_t renderingLayerMask = 1u;
		uint32_t receiverRenderingLayerMask = kRenderingLayerMaskBits;
	};

	// 保存値を読み書きする
	void from_json(const nlohmann::json& in, DecalProjectorComponent& component);
	void to_json(nlohmann::json& out, const DecalProjectorComponent& component);
}
