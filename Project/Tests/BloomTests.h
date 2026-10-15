#pragma once

namespace NEMTests {

	// 保存と実行中の変更とHDR位置を検証する
	bool TestBloomSettings();
	// 実際のBloomShaderでHDRの広がりと透明度を検証する
	bool TestBloomGPU();
}
