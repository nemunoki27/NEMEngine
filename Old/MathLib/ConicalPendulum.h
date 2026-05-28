#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/Utility/Enum/Easing.h>

//============================================================================
//	ConicalPendulum struct
//============================================================================
namespace SakuEngine {

	struct ConicalPendulum {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Vector3 anchor;     // 支点の位置
		Vector3 currentPos; // 現在の位置

		float length;          // 長さ
		float halfApexAngle;   // 円錐の頂点の半分
		float angle;           // 現在の角度
		float angularVelocity; // 角速度

		float moveSpeed;       // 移動速度
		EasingType easingType; // イージング
		float easedAngle;      // イージング後の角度

		float minAngle; // 最小角度
		float maxAngle; // 最大角度

		bool isDrawDebug = false;  // デバッグ描画するか
		bool isEditUpdate = false; // エディターで更新するか

		// 回転
		Quaternion rotation;

		// min、maxに到達した回数
		uint32_t reachCount;

		//--------- functions ----------------------------------------------------

		// min、maxAngleの位置を取得する
		Vector3 GetMinPos() const;
		Vector3 GetMaxPos() const;

		// 初期化
		void Init();
		// リセット
		void Reset(bool isStartMin);

		// 更新
		void Update();

		// デバッグ描画
		void DrawDebugLine();

		// エディター
		void ImGui();

		// json
		void FromJson(const Json& data);
		void ToJson(Json& data);
	};

}; // SakuEngine
