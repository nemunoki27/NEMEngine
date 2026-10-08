#pragma once

namespace NEMTests {

	// 文書の外部参照更新と失敗時の保持を確認する
	bool CheckModelDocumentReferences();

	// Projectの複製とコピーで共有参照を維持する
	bool CheckProjectModelReferenceCopy();

	// OBJの共有Materialを複製・移動・削除で保持する
	bool CheckProjectOBJReferenceOperations();
}
