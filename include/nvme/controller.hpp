#pragma once

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
#include <type_traits>
#include <unordered_map>

#include "../dma/heap.hpp"
#include "../os/mmio.hpp"
#include "../utils.hpp"
#include "cid.hpp"
#include "nvme/queue_entry.hpp"
#include "queue_pair.hpp"
#include "registers.hpp"
#include "spec.hpp"

namespace nvme
{

using namespace std::chrono_literals;

template <typename Command>
concept CommandType = std::default_initializable<Command>;

template <typename Fn, typename Command>
concept CommandBuilder = std::invocable<Fn, Command&> && std::is_void_v<std::invoke_result_t<Fn, Command&>>;

struct NvmeDeviceHandle
{
    os::MmioRegion bar0;
    dma::DmaHeap&  dma;
};

struct InFlightCommand
{
    Cid                            cid;
    std::optional<CompletionEntry> result;
    CommonCommand                  cmd;
};

struct ControllerDiagnostic
{
    enum class Severity
    {
        INFO,
        WARN,
        ERROR
    };

    std::string reason;
    Severity    severity;
};

class Controller final
{
  private:
    static constexpr std::uint8_t CtlEn  = 0b1;
    static constexpr std::uint8_t CtlDis = 0b0;

  public:
    explicit Controller(NvmeDeviceHandle device)
            : regs_{device.bar0}, admin_queue_pair_{device.dma, regs_.doorbells(AdminQueuePair::AQ_ID)},
              timeout_{125ms}, admin_cid_allocator_{constants::AQ_DEPTH} // max 32 elements could be present at a queue
    {
        const auto vs    = regs_.vs();
        const auto minor = vs::MinorVersion::extract(vs);
        const auto major = vs::MajorVersion::extract(vs);

        std::println("[INFO]: NVMe Version {}.{}", major, minor);
    }

    ~Controller()
    {
        disable();
    }

    Controller(const Controller&)            = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&)                 = delete;
    Controller& operator=(Controller&&)      = delete;

    void enable()
    {
        change_controller_state_to(CtlEn);
    }

    void disable()
    {
        change_controller_state_to(CtlDis);
    }

    void configure()
    {
        fetch_timeout();
        disable();

        const std::uint64_t cap = regs_.cap();

        // Set CC.AMS
        std::uint8_t ccAms{0};

        const auto capAms = cap::AMS::extract(cap);

        if (capAms == 0) {
            std::println("AMS: Vendor specific.");
            ccAms = 0b0; // TODO: 0b111 or 0b0?
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
        const auto capMQES   = cap::MQES::extract(cap);

        std::println("MPSMax: {}\nMPSMin: {}", capMPSMax, capMPSMin);
        std::println("Max Queue Entry Size (CAP.MQES): {}", capMQES);

        if (ccMps < capMPSMin || ccMps > capMPSMax) {
            ccMps = static_cast<std::uint8_t>(capMPSMin);
            std::println("[INFO]: ccMps {} is out of bounds, setting it as "
                         "capMPSMin: {}",
                         ccMps, capMPSMin);
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
        assert(asqb % mps_page == 0 && "ASQB shall be memory page aligned (based on the value in CC.MPS)");
        assert(acqb % mps_page == 0 && "ACQB shall be memory page aligned (based on the value in CC.MPS)");

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

    // submits an admin command that is built via the builder function provided
    // NOTE: waits for its result as well, this is *blocking*
    template <CommandType Command, CommandBuilder<Command> Fn>
    std::expected<CompletionEntry, ControllerDiagnostic> execute_admin_command(Fn&& cmd_builder_fn)
    {
        auto submitted = submit_admin_command<Command>(std::forward<Fn>(cmd_builder_fn));
        if (!submitted)
            return std::unexpected{submitted.error()};

        return poll_admin_command(*submitted);
    }

    template <CommandType Command, CommandBuilder<Command> Fn>
    std::expected<Cid::value_type, ControllerDiagnostic> submit_admin_command(Fn&& fn)
    {
        Command c{};
        fn(c);

        Cid cmd_id = admin_cid_allocator_.allocate();
        auto cid_val = cmd_id.get();

        CommonCommand cmd    = c.build(cid_val);

        auto& asq = admin_queue_pair_.sq();
        asq.push(cmd);

        // TODO: should cmd be moved into the map asw? CommonCommand is 64 bytes wide, i would say not trivially copyable?
        auto ins = in_flight_cmds_.try_emplace(cid_val, InFlightCommand{std::move(cmd_id), std::nullopt, cmd});
        if (!ins.second) {
            // we have a problem: we submit a command but now we got it again?
            return std::unexpected{ControllerDiagnostic{"Encountered an already submitted CID",
                                                        ControllerDiagnostic::Severity::ERROR}};
        }

        return cid_val;
    }

    // TODO: this was Cid cid before but Cid is only moveable therefore it didn't make any sense.
    //       therefore make Cid really a handle -> i.e. a copyable one (cheap to copy)
    std::expected<CompletionEntry, ControllerDiagnostic> poll_admin_command(Cid::value_type cid)
    {
        auto& acq = admin_queue_pair_.cq();
        auto& asq = admin_queue_pair_.sq();

        const auto waited = in_flight_cmds_.find(cid);
        if (waited == in_flight_cmds_.end()) {
            return std::unexpected{ControllerDiagnostic{std::format("Polling for CID {}, which is not in flight", cid),
                                                        ControllerDiagnostic::Severity::ERROR}};
        }

        // an earlier poll for some other command could place the result into the flight buffer
        if (waited->second.result.has_value()) {
            const CompletionEntry completion = *waited->second.result;
            in_flight_cmds_.erase(waited);
            return completion;
        }

        const auto deadline = std::chrono::steady_clock::now() + constants::ADMIN_CMD_TIMEOUT;

        while (true) {
            const std::optional<CompletionEntry> completion = acq.poll();

            if (!completion.has_value()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    // NOTE: the command is still in flight so the CID should NOT be released
                    return std::unexpected{ControllerDiagnostic{std::format("Timed out after {}s waiting for CID {}",
                                                                            constants::ADMIN_CMD_TIMEOUT.count(), cid),
                                                                ControllerDiagnostic::Severity::ERROR}};
                }

                std::this_thread::yield();
                continue;
            }

            asq.advance_head(completion->sq_head);

            const auto it = in_flight_cmds_.find(completion->cid);
            if (it == in_flight_cmds_.end()) {
                return std::unexpected{ControllerDiagnostic{
                        std::format("Completion for CID {}, which was never submitted", completion->cid),
                        ControllerDiagnostic::Severity::ERROR}};
            }

            InFlightCommand& entry = it->second;
            if (entry.result.has_value()) {
                return std::unexpected{ControllerDiagnostic{
                        std::format("Duplicate completion for CID {} (opcode {}), which was already completed",
                                    completion->cid, sqe::cdw0::OPC::extract(entry.cmd.cdw0)),
                        ControllerDiagnostic::Severity::ERROR}};
            }

            if (completion->cid != cid) {
                // not our command, hand the observed result to the owner's slot
                entry.result = completion;
                continue;
            }

            // release CID as there are no in flight commands for it now
            // as in flight commands owns it should call the destructor
            in_flight_cmds_.erase(it);
            return *completion;
        }
    }

    AdminQueuePair& admin_queues()
    {
        return admin_queue_pair_;
    }

  private:
    bool wait_for(std::predicate auto&& predicate, std::chrono::milliseconds timeout)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;

            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    bool rdy_is(unsigned want)
    {
        return csts::RDY::extract(regs_.csts()) == want;
    }

    void fetch_timeout()
    {
        const auto timeout_raw = cap::TIMEOUT::extract(regs_.cap());
        timeout_               = timeout_raw * 500ms;
    }

    // en = 0b0 -> disable
    // en = 0b1 -> enable
    void change_controller_state_to(std::uint8_t en)
    {
        const auto cc = cc::EN::insert(regs_.cc(), en);
        regs_.cc(cc);

        if (!wait_for([&] { return rdy_is(en); }, timeout_)) {
            if (en == 0b0)
                std::println("[TIMEOUT]: Controller did not become disabled.");
            else
                std::println("[TIMEOUT]: Controller did not become enabled.");
            return;
        }

        if (en == 0b0)
            std::println("[INFO] Controller is disabled.");
        else
            std::println("[INFO] Controller is enabled.");
    }

  private:
    ControllerRegisters       regs_;
    AdminQueuePair            admin_queue_pair_;
    std::chrono::milliseconds timeout_;

    CidAllocator admin_cid_allocator_;

    std::unordered_map<Cid::value_type, InFlightCommand> in_flight_cmds_;
};

} // namespace nvme
