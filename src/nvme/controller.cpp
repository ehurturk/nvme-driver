#include "nvme/controller.hpp"

#include "nvme/cid.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/queue_pair.hpp"
#include "nvme/spec.hpp"
#include "utils.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <print>
#include <thread>

namespace nvme {

Controller::Controller(NvmeDeviceHandle device)
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

Controller::~Controller() {
    disable();
}

void Controller::enable() {
    change_controller_state_to(CtlEn);
}

void Controller::disable() {
    change_controller_state_to(CtlDis);
}

void Controller::configure() {
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

    if ((capCss & cap::css::NOIOCSS_BIT) != 0u) {
        std::println("No IO command set is supported. Setting CC.CSS to NOIOCSS (0b111)");
        ccCss = cc::css::NOIOCSS;
    } else if ((capCss & cap::css::IOCSS_BIT) != 0u) {
        std::println("IO Command Set is supported. Setting CC.CSS to IOCSS (0b110)");
        ccCss = cc::css::IOCSS;
    } else if ((capCss & cap::css::NCSS_BIT) != 0u) {
        std::println("NVM Command Set is supported. Setting CC.CSS to NCSS (0b000)");
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
        asqb % mps_page == 0 && "ASQB shall be memory page aligned (based on the value in CC.MPS)"
    );
    assert(
        acqb % mps_page == 0 && "ACQB shall be memory page aligned (based on the value in CC.MPS)"
    );

    regs_.asq(asqb);
    regs_.acq(acqb);

    // CC: AMS, MPS, CSS + queue entry sizes

    utils::DwordBuilder<std::uint32_t> cc_builder{regs_.cc()};
    cc_builder.set<cc::AMS>(ccAms);
    cc_builder.set<cc::MPS>(ccMps);
    cc_builder.set<cc::CSS>(ccCss);
    cc_builder.set<cc::IOSQES>(static_cast<std::uint32_t>(constants::IO_SQES));
    cc_builder.set<cc::IOCQES>(static_cast<std::uint32_t>(constants::IO_CQES));
    regs_.cc(cc_builder.get());

    enable();
}

AdminQueuePair& Controller::admin_queues() {
    return admin_queue_pair_;
}

std::uint32_t Controller::max_io_queue_entries() const {
    return static_cast<std::uint32_t>(cap::MQES::extract(regs_.cap())) + 1;
}

bool Controller::requires_contiguous_queues() const {
    return cap::CQR::extract(regs_.cap()) == 1;
}

Result<wire::CompletionEntry> Controller::poll_admin_command(Cid::value_type cid) {
    if (!in_flight_cmds_.contains(cid)) {
        return std::unexpected{Error{
            .reason = std::format("Polling for CID {}, which is not in flight", cid),
            .severity = Error::Severity::ERROR
        }};
    }

    const auto deadline = std::chrono::steady_clock::now() + constants::ADMIN_CMD_TIMEOUT;

    while (true) {
        if (auto done = take_completion(cid)) {
            return *done;
        }

        const std::optional<wire::CompletionEntry> completion = admin_queue_pair_.cq().poll();
        if (!completion.has_value()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // NOTE: the command is still in flight so the cid should not be released
                return std::unexpected{Error{
                    .reason = std::format(
                        "Timed out after {}s waiting for CID {}",
                        constants::ADMIN_CMD_TIMEOUT.count(),
                        cid
                    ),
                    .severity = Error::Severity::ERROR
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

std::optional<Result<wire::CompletionEntry>>
Controller::try_poll_admin_command(Cid::value_type cid) {
    if (!in_flight_cmds_.contains(cid)) {
        return std::unexpected{Error{
            .reason = std::format("Polling for CID {}, which is not in flight", cid),
            .severity = Error::Severity::ERROR
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

void Controller::abandon_admin_command(Cid::value_type cid) {
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

std::optional<Error> Controller::accept_completion(const wire::CompletionEntry& completion) {
    const auto it = in_flight_cmds_.find(completion.cid);
    if (it == in_flight_cmds_.end()) {
        return Error{
            .reason =
                std::format("Completion for CID {}, which was never submitted", completion.cid),
            .severity = Error::Severity::ERROR
        };
    }

    InFlightCommand& entry = it->second;
    if (entry.abandoned) {
        // nobody is waiting on this result any more, might as well drop it and release the CID
        in_flight_cmds_.erase(it);
        return std::nullopt;
    }

    if (entry.result.has_value()) {
        return Error{
            .reason = std::format(
                "Duplicate completion for CID {} (opcode {}), which was already completed",
                completion.cid,
                entry.cmd.opcode()
            ),
            .severity = Error::Severity::ERROR
        };
    }

    entry.result = completion;
    return std::nullopt;
}

std::optional<wire::CompletionEntry> Controller::take_completion(Cid::value_type cid) {
    const auto it = in_flight_cmds_.find(cid);
    if (it == in_flight_cmds_.end() || !it->second.result.has_value()) {
        return std::nullopt;
    }

    const wire::CompletionEntry completion = *it->second.result;
    in_flight_cmds_.erase(it);
    return completion;
}

bool Controller::rdy_is(unsigned want) {
    return csts::RDY::extract(regs_.csts()) == want;
}

void Controller::fetch_timeout() {
    const auto timeout_raw = cap::TIMEOUT::extract(regs_.cap());
    timeout_ = timeout_raw * 500ms;
}

void Controller::change_controller_state_to(std::uint8_t en) {
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

}  // namespace nvme
