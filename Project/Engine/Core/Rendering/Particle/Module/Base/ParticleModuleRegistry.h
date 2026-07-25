#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>

// c++
#include <memory>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleModuleRegistry class
	//	Builtinモジュールの記述子を連続IDで管理し、アセットの文字列IDを一度だけ解決する
	//============================================================================
	class ParticleModuleRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using CreateFunc = std::unique_ptr<IParticleModule>(*)();
		using TypeID = uint16_t;
		static constexpr TypeID kInvalidTypeID = (std::numeric_limits<TypeID>::max)();

		struct Descriptor {

			TypeID typeID = kInvalidTypeID;
			std::string id;
			CreateFunc create = nullptr;
		};

		// モジュールを登録する、登録済みなら何もしない
		TypeID Register(std::string id, CreateFunc create);

		// IDからモジュールを生成する、未登録ならnullptr
		std::unique_ptr<IParticleModule> Create(const std::string& id) const;
		// 解決済みの型IDからモジュールを生成する
		std::unique_ptr<IParticleModule> Create(TypeID typeID) const;
		// 文字列IDを実行時の型IDへ解決する
		TypeID FindTypeID(const std::string& id) const;

		//--------- accessor -----------------------------------------------------

		const std::vector<Descriptor>& GetDescriptors() const { return descriptors_; }

		// シングルトン
		static ParticleModuleRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		ParticleModuleRegistry();

		//--------- variables ----------------------------------------------------

		// 数値IDで直接参照する連続した記述子
		std::vector<Descriptor> descriptors_;
		// アセットの文字列IDを数値IDへ解決するマップ
		std::unordered_map<std::string, TypeID> typeIDs_;
	};
} // Engine
