#pragma once

namespace NEMTests {

	// 数学型の演算と保存値を検証する
	bool TestMathContracts();
	// 行列分解の再構成と失敗時の保持を検証する
	bool TestAffineDecomposition();
}
