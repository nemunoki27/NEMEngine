#pragma once

//============================================================================
//	ViewportDecalGizmo
//============================================================================
namespace Engine {

	class ECSWorld;
	struct Entity;
	class LineRenderer3D;

	namespace ViewportDecalGizmo {

		// 選択した投影の範囲と方向を表示する
		void Draw(ECSWorld& world, const Entity& entity, LineRenderer3D& lines);
	}
}
