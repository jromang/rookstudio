#pragma once

// An engine of the library answering the assistant's questions (commentary::Oracle): what a side would play, a move's
// worth, the threat. A process of its own, one thread, a fixed number of nodes per search (300,000, as the prototype
// confirmed its facts), each from an empty hash, started at the first question and kept until the oracle goes; so its
// answers depend neither on the machine's load, nor on the Board page's analysis, nor on the questions before.

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

#include <commentary/oracle.hpp>

#include "engine_client.hpp"
#include "model.hpp"

namespace fcg {

class EngineOracle final : public commentary::Oracle {
   public:
    explicit EngineOracle(EngineEntry engine, uint64_t nodes = 300000);
    ~EngineOracle() override;
    EngineOracle(const EngineOracle&)            = delete;
    EngineOracle& operator=(const EngineOracle&) = delete;

    [[nodiscard]] std::optional<commentary::Searched> search(const commentary::Position& pos,
                                                             std::span<const commentary::Move> only) const override;
    // Why the last search gave nothing: the engine did not start, did not answer in time.
    [[nodiscard]] std::string error() const;
    [[nodiscard]] const std::string& engine() const noexcept { return engine_.name; }

   private:
    bool ready() const;  // started, spoken to in UCI, its one thread set; under mutex_

    EngineEntry engine_;
    uint64_t nodes_;
    mutable std::mutex mutex_;
    mutable std::unique_ptr<EngineProcess> process_;
    mutable bool chess960_ = false;
    mutable std::string error_;
};

}  // namespace fcg
