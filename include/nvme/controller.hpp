#pragma once

#include "../dma/heap.hpp"
#include "../os/mmio.hpp"
#include "../utils.hpp"
#include "cid.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "queue_pair.hpp"
#include "registers.hpp"
#include "spec.hpp"

#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace nvme {

using namespace std::chrono_literals;

struct NvmeDeviceHandle {
    os::MmioRegion bar0;
    dma::DmaHeap& dma;
};

struct InFlightCommand {
    Cid cid;
    std::optional<CompletionQueueEntry> result;
    CommonCommand cmd;
    // set when the CommandPending for this command was dropped without consuming
    // the result: the entry (and its CID) is freed as soon as the completion arrives
    bool abandoned = false;
};

struct ControllerDiagnostic {
    enum class Severity {
        INFO,
        WARN,
        ERROR
    };

    std::string reason;
    Severity severity;
};

template<CommandDescriptor Command>
class CommandPending;

class Controller final {
private:
    static constexpr std::uint8_t CtlEn = 0b1;
    static constexpr std::uint8_t CtlDis = 0b0;

public:
    explicit Controller(NvmeDeviceHandle device)
        : regs_{device.bar0},
          admin_queue_pair_{device.dma, regs_.doorbells(AdminQueuePair::AQ_ID)},
          timeout_{125ms},
          admin_cid_allocator_{constants::AQ_DEPTH}  // max 32 elements could be present at a queue
    {
        const auto vs = regs_.vs();
        const auto minor = vs::MinorVersion::extract(vs);
        const auto major = vs::MajorVersion::extract(vs);

        std::println("[INFO]: NVMe Version {}.{}", major, minor);
    }

    ~Controller() {
        disable();
    }

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    void enable() {
        change_controller_state_to(CtlEn);
    }

    void disable() {
        change_controller_state_to(CtlDis);
    }

    void configure() {
        fetch_timeout();
        disable();

        const std::uint64_t cap = regs_.cap();

        // Set CC.AMS
        std::uint8_t ccAms{0};

        const auto capAms = cap::AMS::extract(cap);

        if (capAms == 0) {
            std::println("AMS: Vendor specific.");
            ccAms = 0b0;  // TODO: 0b111 or 0b0?
        } else if (capAms == 1) {
            std::println("AMS: WRRUPC");
            ccAms = 0b1;
        } else {
            std::println("Unsupported AMS. Defaulting to round robin.");
            ccAms = 0b0;
        }

        // Set CC.MPS
        std::uint8_t ccMps{0};

        const auto capMPSMax = cap::MPSMax::extract(cap);
        const auto capMPSMin = cap::MPSMin::extract(cap);
        const auto capMQES = cap::MQES::extract(cap);

        std::println("MPSMax: {}\nMPSMin: {}", capMPSMax, capMPSMin);
        std::println("Max Queue Entry Size (CAP.MQES): {}", capMQES);

        if (ccMps < capMPSMin || ccMps > capMPSMax) {
            ccMps = static_cast<std::uint8_t>(capMPSMin);
            std::println(
                "[INFO]: ccMps {} is out of bounds, setting it as "
                "capMPSMin: {}",
                ccMps,
                capMPSMin
            );
        }

        // Set CC.CSS
        std::uint8_t ccCss{0};

        const auto capCss = cap::CSS::extract(cap);

        if (capCss & cap::css::NCSS_BIT) {
            std::println("NCSS Bit is present...");
            ccCss = cc::css::NCSS;
        } else {
            std::println("NVME Command Set is not found.");
            return;
        }

        // AQA, NOTE: NVMe controller expects 0s-based depth!!!!
        utils::DwordBuilder<std::uint32_t> aqa_builder{regs_.aqa()};
        aqa_builder.set<aqa::ASQS>(static_cast<std::uint32_t>(constants::AQ_DEPTH - 1));
        aqa_builder.set<aqa::ACQS>(static_cast<std::uint32_t>(constants::AQ_DEPTH - 1));
        regs_.aqa(aqa_builder.get());

        const std::uint64_t asqb = admin_queue_pair_.sq_iova();
        const std::uint64_t acqb = admin_queue_pair_.cq_iova();

        const std::uint64_t mps_page = std::uint64_t{1} << (12 + ccMps);
        assert(
            asqb % mps_page == 0 &&
            "ASQB shall be memory page aligned (based on the value in CC.MPS)"
        );
        assert(
            acqb % mps_page == 0 &&
            "ACQB shall be memory page aligned (based on the value in CC.MPS)"
        );

        regs_.asq(asqb);
        regs_.acq(acqb);

        // CC: AMS, MPS, CSS + queue entry sizes

        // For creating the admin queues, IOSQES and IOCQES are: 6 and 4, (so 64 bytes and 16 bytes)
        // after Identify Controller, these get adjusted to the required minimums for the I/O queues
        utils::DwordBuilder<std::uint32_t> cc_builder{regs_.cc()};
        cc_builder.set<cc::AMS>(ccAms);
        cc_builder.set<cc::MPS>(ccMps);
        cc_builder.set<cc::CSS>(ccCss);
        cc_builder.set<cc::IOSQES>(static_cast<std::uint32_t>(constants::IO_SQES));
        cc_builder.set<cc::IOCQES>(static_cast<std::uint32_t>(constants::IO_CQES));
        regs_.cc(cc_builder.get());

        enable();
    }

    // submits an admin command and blocks until its own completion arrives
    template<CommandDescriptor Command>
    std::expected<CompletionQueueEntry, ControllerDiagnostic> execute_admin_command(Command cmd) {
        auto submitted = submit_admin_command(std::move(cmd));
        if (!submitted) {
            return std::unexpected{submitted.error()};
        }

        return submitted->wait();
    }

    // pushes the command into the admin SQ
    // NOTE: dropping the handle abandons the command
    template<CommandDescriptor Command>
    std::expected<CommandPending<Command>, ControllerDiagnostic>
    submit_admin_command(Command command) {
        Cid cmd_id = admin_cid_allocator_.allocate();
        auto cid_val = cmd_id.get();

        CommonCommand cmd = command.build(cid_val);

        auto& asq = admin_queue_pair_.sq();
        asq.push(cmd);

        auto [it, inserted] = in_flight_cmds_.try_emplace(
            cid_val,
            InFlightCommand{.cid = std::move(cmd_id), .result = std::nullopt, .cmd = cmd}
        );

        if (!inserted) {
            // we have a problem: we submit a command but now we got it again?
            return std::unexpected{ControllerDiagnostic{
                .reason = "Encountered an already submitted CID",
                .severity = ControllerDiagnostic::Severity::ERROR
            }};
        }

        return CommandPending<Command>{*this, cid_val};
    }

    AdminQueuePair& admin_queues() {
        return admin_queue_pair_;
    }

private:
    template<CommandDescriptor Command>
    friend class CommandPending;

    // blockingly polls the admin CQ until the completion for cid arrives or the timeout expires
    std::expected<CompletionQueueEntry, ControllerDiagnostic>
    poll_admin_command(Cid::value_type cid) {
        if (!in_flight_cmds_.contains(cid)) {
            return std::unexpected{ControllerDiagnostic{
                .reason = std::format("Polling for CID {}, which is not in flight", cid),
                .severity = ControllerDiagnostic::Severity::ERROR
            }};
        }

        const auto deadline = std::chrono::steady_clock::now() + constants::ADMIN_CMD_TIMEOUT;

        while (true) {
            if (auto done = take_completion(cid)) {
                return *done;
            }

            const std::optional<CompletionQueueEntry> completion = admin_queue_pair_.cq().poll();
            if (!completion.has_value()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    // NOTE: the command is still in flight so the cid should NOT be released
                    return std::unexpected{ControllerDiagnostic{
                        .reason = std::format(
                            "Timed out after {}s waiting for CID {}",
                            constants::ADMIN_CMD_TIMEOUT.count(),
                            cid
                        ),
                        .severity = ControllerDiagnostic::Severity::ERROR
                    }};
                }

                std::this_thread::yield();
                continue;
            }

            admin_queue_pair_.sq().advance_head(completion->sq_head);
            if (auto diag = accept_completion(*completion)) {
                return std::unexpected{std::move(*diag)};
            }
        }
    }

    // non-blockingly drains whatever completions are visible on the admin CQ
    // nullopt means the command for cid is still in flight
    std::expected<std::optional<CompletionQueueEntry>, ControllerDiagnostic>
    try_poll_admin_command(Cid::value_type cid) {
        if (!in_flight_cmds_.contains(cid)) {
            return std::unexpected{ControllerDiagnostic{
                .reason = std::format("Polling for CID {}, which is not in flight", cid),
                .severity = ControllerDiagnostic::Severity::ERROR
            }};
        }

        while (const auto completion = admin_queue_pair_.cq().poll()) {
            admin_queue_pair_.sq().advance_head(completion->sq_head);
            if (auto diag = accept_completion(*completion)) {
                return std::unexpected{std::move(*diag)};
            }
        }

        return take_completion(cid);
    }

    void abandon_admin_command(Cid::value_type cid) {
        const auto it = in_flight_cmds_.find(cid);
        if (it == in_flight_cmds_.end()) {
            return;
        }

        if (it->second.result.has_value()) {
            in_flight_cmds_.erase(it);
        } else {
            it->second.abandoned = true;
        }
    }

    // mark completion's cid as completed and write its result to in flight command map
    std::optional<ControllerDiagnostic> accept_completion(const CompletionQueueEntry& completion) {
        const auto it = in_flight_cmds_.find(completion.cid);
        if (it == in_flight_cmds_.end()) {
            return ControllerDiagnostic{
                .reason =
                    std::format("Completion for CID {}, which was never submitted", completion.cid),
                .severity = ControllerDiagnostic::Severity::ERROR
            };
        }

        InFlightCommand& entry = it->second;
        if (entry.abandoned) {
            // might as well release the cid if no one waits for it
            in_flight_cmds_.erase(it);
            return std::nullopt;
        }

        if (entry.result.has_value()) {
            return ControllerDiagnostic{
                .reason = std::format(
                    "Duplicate completion for CID {} (opcode {}), which was already completed",
                    completion.cid,
                    entry.cmd.opcode()
                ),
                .severity = ControllerDiagnostic::Severity::ERROR
            };
        }

        // completed
        entry.result = completion;
        return std::nullopt;
    }

    // return the completion queue entry for a given cid and release that cid
    std::optional<CompletionQueueEntry> take_completion(Cid::value_type cid) {
        const auto it = in_flight_cmds_.find(cid);
        if (it == in_flight_cmds_.end() || !it->second.result.has_value()) {
            return std::nullopt;
        }

        const CompletionQueueEntry completion = *it->second.result;
        in_flight_cmds_.erase(it);
        return completion;
    }

    bool wait_for(std::predicate auto&& predicate, std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }

            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    bool rdy_is(unsigned want) {
        return csts::RDY::extract(regs_.csts()) == want;
    }

    void fetch_timeout() {
        const auto timeout_raw = cap::TIMEOUT::extract(regs_.cap());
        timeout_ = timeout_raw * 500ms;
    }

    // en = 0b0 -> disable
    // en = 0b1 -> enable
    void change_controller_state_to(std::uint8_t en) {
        const auto cc = cc::EN::insert(regs_.cc(), en);
        regs_.cc(cc);

        if (!wait_for(
                [&] {
                    return rdy_is(en);
                },
                timeout_
            )) {
            if (en == 0b0) {
                std::println("[TIMEOUT]: Controller did not become disabled.");
            } else {
                std::println("[TIMEOUT]: Controller did not become enabled.");
            }
            return;
        }

        if (en == 0b0) {
            std::println("[INFO] Controller is disabled.");
        } else {
            std::println("[INFO] Controller is enabled.");
        }
    }

private:
    ControllerRegisters regs_;
    AdminQueuePair admin_queue_pair_;
    std::chrono::milliseconds timeout_;

    CidAllocator admin_cid_allocator_;

    std::unordered_map<Cid::value_type, InFlightCommand> in_flight_cmds_;
};

template<CommandDescriptor Command>
class CommandPending {
public:
    CommandPending(const CommandPending&) = delete;
    CommandPending& operator=(const CommandPending&) = delete;

    CommandPending(CommandPending&& other) noexcept
        : controller_{std::exchange(other.controller_, nullptr)},
          cid_{other.cid_},
          completion_{other.completion_} {
    }

    CommandPending& operator=(CommandPending&& other) noexcept {
        if (this != &other) {
            release();
            controller_ = std::exchange(other.controller_, nullptr);
            cid_ = other.cid_;
            completion_ = other.completion_;
        }
        return *this;
    }

    ~CommandPending() {
        release();
    }

    Cid::value_type cid() const {
        return cid_;
    }

    bool done() const {
        return completion_.has_value();
    }

    // non-blockingly drains whatever completions are visible on the admin CQ
    // nullopt means the command for cid is still in flight
    std::expected<std::optional<CompletionQueueEntry>, ControllerDiagnostic> poll() {
        if (completion_.has_value()) {
            return completion_;
        }
        if (controller_ == nullptr) {
            return std::unexpected{empty_handle_diagnostic()};
        }

        auto polled = controller_->try_poll_admin_command(cid_);
        if (polled.has_value() && polled->has_value()) {
            completion_ = **polled;
        }
        return polled;
    }

    // blockingly polls the admin CQ until the completion for cid arrives or the timeout expires
    std::expected<CompletionQueueEntry, ControllerDiagnostic> wait() {
        if (completion_.has_value()) {
            return *completion_;
        }
        if (controller_ == nullptr) {
            return std::unexpected{empty_handle_diagnostic()};
        }

        auto waited = controller_->poll_admin_command(cid_);
        if (waited.has_value()) {
            completion_ = *waited;
        }
        return waited;
    }

    // blockingly waits and decodes the completion with this command's own decoder
    std::expected<typename Command::Completion, ControllerDiagnostic> result() {
        auto waited = wait();
        if (!waited.has_value()) {
            return std::unexpected{std::move(waited.error())};
        }

        if (!waited->ok()) {
            return std::unexpected{ControllerDiagnostic{
                .reason =
                    std::format("CID {} completed with error status: {}", cid_, waited->status),
                .severity = ControllerDiagnostic::Severity::ERROR
            }};
        }

        return Command::decode_completion(*waited);
    }

private:
    friend class Controller;

    CommandPending(Controller& controller, Cid::value_type cid)
        : controller_{&controller}, cid_{cid} {
    }

    void release() {
        if (controller_ != nullptr && !completion_.has_value()) {
            controller_->abandon_admin_command(cid_);
        }
        controller_ = nullptr;
    }

    ControllerDiagnostic empty_handle_diagnostic() const {
        return ControllerDiagnostic{
            .reason = std::format("CommandPending for CID {} is empty", cid_),
            .severity = ControllerDiagnostic::Severity::ERROR
        };
    }

    Controller* controller_ = nullptr;
    Cid::value_type cid_{};
    std::optional<CompletionQueueEntry> completion_;
};

}  // namespace nvme
