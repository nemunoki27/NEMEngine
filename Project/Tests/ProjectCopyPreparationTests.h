#pragma once

//============================================================================
//	include
//============================================================================
namespace NEMTests {

	// 作業byteの編集失敗と外部差替えを検証する
	bool TestProjectCopyPreparationOwnership();
	// フォルダー差替えを子ファイルの処理へ持ち込まない
	bool TestProjectDirectoryCopyOwnership();
}
