#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
#include <string_view>

namespace Engine::ShaderGraphStageSource {

	// 全描画経路で共有するグラフ入力を宣言する
	std::string BuildSurfaceInputDeclaration();
	// 時間と描画Cameraの共有定数を宣言する
	std::string BuildTimeConstantsDeclaration();
	// 転送されたCameraと行列を入力へ渡す
	std::string BuildFrameInputAssignments(std::string_view input = "graphInput");
	// オブジェクト行列と描画境界を入力へ渡す
	std::string BuildObjectInputAssignments(std::string_view worldMatrix, std::string_view boundsMinimum,
		std::string_view boundsMaximum, std::string_view input = "graphInput");
	// 描画で使用する境界球をワールド境界へ変換する
	std::string BuildSphereBoundsAssignments(std::string_view center, std::string_view radius,
		std::string_view worldMatrix, std::string_view input = "graphInput");
	// 平面形状の矩形境界をワールド境界へ変換する
	std::string BuildBoxBoundsAssignments(std::string_view minimum, std::string_view maximum,
		std::string_view worldMatrix, std::string_view input = "graphInput");
} // Engine::ShaderGraphStageSource
