#pragma once

namespace NEMTests {

	// Cook入力外のShaderとIncludeを拒否することを確認する
	bool TestShaderCookInputs();
	// 同時要求とキャッシュ復元後のReflectionを確認する
	bool TestShaderCache(unsigned expectedWidth);
	// Graph編集による生成Includeの変更をキャッシュへ反映する
	bool TestShaderGraphCache();
}
