#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleModule.h>

// c++
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleModuleRegistry class
	//	文字列IDからモジュールを生成する、各モジュールは自己登録する
	//============================================================================
	class ParticleModuleRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using CreateFunc = std::unique_ptr<IParticleModule>(*)();

		// モジュールを登録する、登録済みなら何もしない
		uint32_t Register(const std::string& id, CreateFunc create);

		// IDからモジュールを生成する、未登録ならnullptr
		std::unique_ptr<IParticleModule> Create(const std::string& id) const;

		//--------- accessor -----------------------------------------------------

		// 登録済みのID一覧を名前順で取得する、エディターの追加候補に使う
		std::vector<std::string> GetRegisteredIDs() const;

		// シングルトン
		static ParticleModuleRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// IDから生成関数へのマップ
		std::unordered_map<std::string, CreateFunc> creators_;
	};

	//============================================================================
	//	ParticleModuleRegistry macros
	//============================================================================
#define ENGINE_REGISTER_PARTICLE_MODULE(T, IDLiteral) \
    inline const uint32_t kParticleModuleID_##T = Engine::ParticleModuleRegistry::GetInstance().Register( \
        IDLiteral, []() -> std::unique_ptr<Engine::IParticleModule> { return std::make_unique<T>(); });
} // Engine
