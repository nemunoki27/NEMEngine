#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	// 描画で渡す定数バッファ
	struct ParticleViewConstants {

		Engine::Matrix4x4 viewProjection = Engine::Matrix4x4::Identity();
		Engine::Vector3 cameraPosition = Engine::Vector3::AnyInit(0.0f);
		float pad0 = 0.0f;
	};
	// パラメトリック形状生成で渡す定数バッファ
	struct ParticleShapeConstants {

		uint32_t divide = 16;
		uint32_t uvMode = 0;
		uint32_t cap = 0;
		uint32_t heightDivide = 2;
	};
	// トレイルMS生成で渡す定数バッファ
	struct ParticleTrailConstants {

		uint32_t segmentCount = 0;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	// MeshShaderの1グループが担当する三角形数
	constexpr uint32_t kParticleMeshGroupTriangles = 64;
	// トレイルMeshShaderの1グループが担当するセグメント数
	constexpr uint32_t kParticleTrailMeshGroupSegments = 32;

}
