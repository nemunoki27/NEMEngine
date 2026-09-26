#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleEmitterDebugDraw class
	//	発生形状の補助線をEditorで描画する
	//============================================================================
	class ParticleEmitterDebugDraw {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 発生形状に対応する補助線を描く
		static void Draw(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 直方体の補助線を描く
		static void DrawBox(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 球の補助線を描く
		static void DrawSphere(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 半球の補助線を描く
		static void DrawHemisphere(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// ドーナツの補助線を描く
		static void DrawTorus(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 点の補助線を描く
		static void DrawPoint(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 円錐の補助線を描く
		static void DrawCone(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 矩形の補助線を描く
		static void DrawRect(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 扇の補助線を描く
		static void DrawCone2D(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
		// 円弧の補助線を描く
		static void DrawCircle(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D);
	};
} // Engine
