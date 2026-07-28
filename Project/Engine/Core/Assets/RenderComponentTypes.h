#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Externals/nlohmann/json_fwd.hpp>

// c++
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace Engine {

	//============================================================================
	//	RenderComponentTypes
	//============================================================================

	// 描画フェーズ
	enum class RenderPhase : uint8_t {

		Opaque,
		Transparent,
		PostProcessMaskedUI,
		ScreenUI,
		EditorOverlay,
		Count
	};
	// Countは実描画フェーズではなく番兵として扱う
	static constexpr size_t kRenderPhaseCount = static_cast<size_t>(RenderPhase::Count);

	// ブレンドモード
	enum BlendMode {

		Normal,   // 通常αブレンド
		Add,      // 加算
		Subtract, // 減算
		Multiply, // 乗算
		Screen,   // スクリーン
	};
	static constexpr const uint32_t kBlendModeCount = static_cast<uint32_t>(BlendMode::Screen) + 1;

	// マテリアルのパラメーター値
	struct MaterialParameterValue {

		std::variant<float, Vector2, Vector3, Vector4, Color4, AssetID, int32_t, uint32_t, bool> value;
	};
	// 型を増減したらpack(MaterialParameterBufferBuilder)/parse/serialize(MaterialAsset)の全変換を更新すること
	static_assert(std::variant_size_v<decltype(MaterialParameterValue::value)> == 9);

	//============================================================================
	//	MaterialParameterOverrides class
	//	空のRendererでMap本体を持たない遅延確保パラメータ集合
	//============================================================================
	class MaterialParameterOverrides {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		using Map = std::unordered_map<std::string, MaterialParameterValue>;
		using iterator = Map::iterator;
		using const_iterator = Map::const_iterator;

		MaterialParameterOverrides() = default;
		~MaterialParameterOverrides() = default;
		MaterialParameterOverrides(const MaterialParameterOverrides& other);
		MaterialParameterOverrides(MaterialParameterOverrides&& other) noexcept = default;
		MaterialParameterOverrides& operator=(const MaterialParameterOverrides& other);
		MaterialParameterOverrides& operator=(MaterialParameterOverrides&& other) noexcept = default;

		MaterialParameterValue& operator[](const std::string& name);
		MaterialParameterValue& operator[](const char* name);
		void clear();
		size_t erase(const std::string& name);
		iterator erase(iterator position);

		//--------- accessor -----------------------------------------------------

		bool empty() const { return !values_ || values_->empty(); }
		size_t size() const { return values_ ? values_->size() : 0; }
		size_t count(const std::string& name) const;
		bool contains(const std::string& name) const;
		iterator begin();
		iterator end();
		const_iterator begin() const;
		const_iterator end() const;
		iterator find(const std::string& name);
		const_iterator find(const std::string& name) const;
		Map& GetMutable();
		const Map& Get() const;
		operator Map&() { return GetMutable(); }
		operator const Map&() const { return Get(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::unique_ptr<Map> values_{};
	};

	// ライン1点の情報、頂点ごとに太さと色を持てる
	struct LinePoint {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 2;
		static constexpr bool kSerializable = false;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;

		Vector3 position = Vector3::AnyInit(0.0f);
		Color4 color = Color4::White();
		// ライン半幅
		float thickness = 1.0f;
	};

	// 単一パラメータ値のjson変換でサブメッシュ側のparameterOverridesでも共用する
	bool ParseMaterialParameterValue(const nlohmann::json& data, MaterialParameterValue& outValue);
	nlohmann::json SerializeMaterialParameterValue(const MaterialParameterValue& parameter);

	// parameterOverridesマップのjson入出力、Mesh/Sprite/Text等の個別マテリアルで共用する
	void ReadMaterialParameterOverrides(const nlohmann::json& in,
		std::unordered_map<std::string, MaterialParameterValue>& outOverrides);
	void ReadMaterialParameterOverrides(const nlohmann::json& in,
		MaterialParameterOverrides& outOverrides);
	nlohmann::json WriteMaterialParameterOverrides(
		const std::unordered_map<std::string, MaterialParameterValue>& overrides);
	nlohmann::json WriteMaterialParameterOverrides(
		const MaterialParameterOverrides& overrides);

	// RenderPhaseの文字列変換、JSON保存やデバッグ表示に使う
	std::string_view ToString(RenderPhase phase);
	bool TryParseRenderPhase(std::string_view value, RenderPhase& outPhase);
	RenderPhase RenderPhaseFromString(std::string_view value, RenderPhase fallback = RenderPhase::Opaque);

	// 各Rendererコンポーネントが共通で持つ描画フィールドのjson入出力、既定値は現在値を使う
	void ReadRenderCommonFields(const nlohmann::json& in,
		int32_t& layer, int32_t& order, bool& visible, BlendMode& blendMode, RenderPhase& queue);
	void WriteRenderCommonFields(nlohmann::json& out,
		int32_t layer, int32_t order, bool visible, BlendMode blendMode, RenderPhase queue);

} // Engine
