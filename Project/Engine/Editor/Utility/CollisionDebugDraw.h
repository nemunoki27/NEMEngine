#pragma once

namespace Engine {

	class ECSWorld;

	// 衝突形状のデバッグ描画
	namespace CollisionDebugDraw {

		// World内の有効なColliderを描画する
		void DrawWorld(ECSWorld& world);
	}
}
