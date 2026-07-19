#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	GameViewCameraSnapshot class
	//	GameViewカメラのフレームスナップショット、スクリプトのScreenPointToRayが参照する
	//============================================================================
	class GameViewCameraSnapshot {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GameViewCameraSnapshot() = default;
		~GameViewCameraSnapshot() = default;

		//--------- structure ----------------------------------------------------

		// 前フレーム描画時点のGameViewカメラ情報
		struct Snapshot {

			Matrix4x4 viewProjection = Matrix4x4::Identity();
			Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
			Vector3 cameraPos = Vector3::AnyInit(0.0f);
			// GameViewの描画解像度
			float width = 0.0f;
			float height = 0.0f;
			bool valid = false;
		};

		//--------- accessor -----------------------------------------------------

		// レンダラーが毎フレーム設定する
		static void Set(const Snapshot& snapshot) { snapshot_ = snapshot; }
		static const Snapshot& Get() { return snapshot_; }
		// ワールド座標をGameViewピクセル座標へ変換する
		static bool TryWorldToScreenPoint(const Vector3& worldPosition, Vector3& outScreenPosition);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 現在のスナップショット
		static Snapshot snapshot_;
	};
} // Engine
