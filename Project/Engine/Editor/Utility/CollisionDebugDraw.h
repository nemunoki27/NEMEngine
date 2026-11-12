#pragma once

namespace Engine {

	class ECSWorld;
	struct Entity;

	// 衝突形状のデバッグ描画
	namespace CollisionDebugDraw {

		// World内の有効なColliderを描画する
		void DrawWorld(ECSWorld& world);
		// 指定したEntityの有効な形状だけ描画する
		void DrawEntity(ECSWorld& world, Entity entity);
	}
}
