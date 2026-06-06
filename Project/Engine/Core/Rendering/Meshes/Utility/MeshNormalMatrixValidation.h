#pragma once

namespace Engine {

	//============================================================================
	//	MeshNormalMatrixValidation
	//============================================================================

	// 環境変数 NEM_MESH_NORMAL_MATRIX_VALIDATE=1 のときだけ、
	// normalMatrixユーティリティのCPU検証(identity/非一様/負/0スケール/NaN/直交性など)を実行する。
	// 要求されていない場合は何もせずtrueを返す。全チェック成功でtrue。
	// 結果はLoggerへ出力する(Debug/Developビルドでの診断用)。
	bool RunMeshNormalMatrixValidationIfRequested();

} // Engine
