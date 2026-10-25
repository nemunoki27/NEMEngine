#pragma once

namespace NEMTests {

	// 表示能力と希望設定の分離を検証する
	bool TestDisplayOutputSettings();
	// 本番の出力変換をGPU上で検証する
	bool TestDisplayOutputGPU();
	// UI合成と外部Windowの表示切替をGPU上で検証する
	bool TestDisplayOutputImGuiGPU();
}
