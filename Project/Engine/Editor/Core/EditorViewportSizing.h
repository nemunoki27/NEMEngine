#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

namespace Engine {

	//============================================================================
	//	EditorViewportSizing class
	//	表示領域が安定してから描画解像度を確定する
	//============================================================================
	class EditorViewportSizing {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Update(const Vector2& imageSize, const Vector2& framebufferScale,
			const Vector2I& referenceSize, double time, bool resizing);

		//--------- accessor -----------------------------------------------------

		Vector2I Resolve(const Vector2I& referenceSize) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Vector2I referenceSize_{};
		Vector2I pendingSize_{};
		Vector2I renderSize_{};
		double changedAt_ = 0.0;
	};
}
