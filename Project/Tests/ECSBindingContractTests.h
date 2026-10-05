#pragma once

namespace NEMTests {

	// 予約Bufferの検証型を登録
	void RegisterECSBindingTestComponents();
	// 予約Bufferの値と個体番号を確認
	bool CheckPendingBuffer();
	// 生成直後のEntityとScript削除を確認
	bool CheckPendingGameObject();
}
