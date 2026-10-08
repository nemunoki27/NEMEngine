#pragma once

namespace NEMTests {

	// Query検証用の型を既存の順序で登録
	void RegisterECSQueryTestComponents();
	// 有効状態とTagと保存用複製を確認
	bool CheckQueryModes();
	// 通知とhook中のWorld終了を確認
	bool CheckNotificationWorldLifetime();
}
