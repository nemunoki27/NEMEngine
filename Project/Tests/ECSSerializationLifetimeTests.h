#pragma once

namespace NEMTests {

	// 保存hookの検証用Componentを登録する
	void RegisterECSSerializationTestComponents();
	// 保存と読込hookの終了と予約取消を確認する
	bool CheckECSSerializationLifetime();
}
