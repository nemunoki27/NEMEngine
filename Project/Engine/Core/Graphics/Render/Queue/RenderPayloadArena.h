#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderPayloadArena structures
	//============================================================================

	// 描画ペイロード参照情報
	struct RenderPayload {

		uint32_t offset = 0;
		uint32_t size = 0;
	};

	//============================================================================
	//	RenderPayloadArena class
	//	描画アイテムのペイロードを格納するアリーナクラス
	//============================================================================
	class RenderPayloadArena {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderPayloadArena() = default;
		~RenderPayloadArena() = default;

		// ペイロードの割り当て
		template <typename T>
		RenderPayload Push(const T& value);
		// データクリア
		void Clear();
		// 必要なバイト数を事前に確保
		void Reserve(uint32_t byteCount);

		//--------- accessor -----------------------------------------------------

		template<class T>
		const T* Get(const RenderPayload& payload) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<std::byte> buffer_;

		//--------- functions ----------------------------------------------------

		// アライメントに基づいて値を切り上げる
		uint32_t AlignUp(uint32_t value, uint32_t alignment);
	};

	//============================================================================
	//	RenderPayloadArena templateMethods
	//============================================================================

	template<typename T>
	inline RenderPayload RenderPayloadArena::Push(const T& value) {

		// データのアライメントを考慮して、バッファの末尾にペイロードを追加する
		const uint32_t align = alignof(T);
		uint32_t offset = AlignUp(static_cast<uint32_t>(buffer_.size()), align);
		// バッファを必要なサイズまで拡張する
		buffer_.resize(offset + sizeof(T));
		std::memcpy(buffer_.data() + offset, &value, sizeof(T));
		return { offset, static_cast<uint32_t>(sizeof(T)) };
	}

	template<class T>
	inline const T* RenderPayloadArena::Get(const RenderPayload& payload) const {

		if (payload.size != sizeof(T)) {
			return nullptr;
		}
		if (buffer_.size() < payload.offset + payload.size) {
			return nullptr;
		}
		return reinterpret_cast<const T*>(buffer_.data() + payload.offset);
	}
} // Engine
