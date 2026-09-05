#include "nvme/controller.hpp"

#include "nvme/cid.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/queue_pair.hpp"
#include "nvme/spec.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <thread>
#include <utility>

namespace nvme {

Controller::Controller(NvmeDeviceHandle device)
    : regs_{device.bar_region()},
      admin_queue_{constants::ADMIN_QID, device.dma_heap(), regs_, constants::AQ_DEPTH},
      timeout_{125ms} {
    const auto vs = regs_.vs();
    const auto minor = vs::MinorVersion::extract(vs);
    const auto major = vs::MajorVersion::extract(vs);

    std::println("[INFO]: NVMe Version {}.{}", major, minor);
}

Controller::~Controller() {
    disable();
}

bool Controller::enable() {
    return change_controller_state_to(CtlEn);
}

bool Controller::disable() {
    return change_controller_state_to(CtlDis);
}

Result<void> Controller::configure() {
    fetch_timeout();

    if (!disable()) {
        return std::unexpected{Error{
            .reason = "Controller did not become disabled within CAP.TO",
            .severity = Error::Severity::ERROR
        }};
    }

    const std::uint64_t cap = regs_.cap();

    // Set CC.AMS
    // CAP.AMS bit 0 = WRR
    //         bit 1 = vendor specific
    // so if CAP.AMS == 0 -> only round robin
    std::uint8_t ccAms{0};

    const auto capAms = cap::AMS::extract(cap);

    if (capAms == 0) {
        std::println("AMS: Round Robin is supported");
    } else if (capAms == 1) {
        std::println("AMS: Weighted Round Robin is supported");
    } else {
        std::println("Unsupported AMS. Defaulting to round robin.");
    }

    // TODO: im setting this to round robin for now but could change this in future ofc
    ccAms = 0;

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

    if ((capCss & cap::css::NCSS_BIT) != 0u) {
        std::println("NVM Command Set is supported. Setting CC.CSS to NCSS (0b000)");
        ccCss = cc::css::NCSS;
    } else if ((capCss & cap::css::IOCSS_BIT) != 0u) {
        std::println("IO Command Sets are supported. Setting CC.CSS to IOCSS (0b110)");
        ccCss = cc::css::IOCSS;
    } else if ((capCss & cap::css::NOIOCSS_BIT) != 0u) {
        std::println("No IO command set is supported. Setting CC.CSS to NOIOCSS (0b111)");
        ccCss = cc::css::NOIOCSS;
    } else {
        return std::unexpected{Error{
            .reason =
                std::format("Controller supports no usable command set (CAP.CSS = {:#x})", capCss),
            .severity = Error::Severity::ERROR
        }};
    }

    // AQA, NOTE: NVMe controller expects 0s-based depth!!!!
    utils::DwordBuilder<std::uint32_t> aqa_builder{regs_.aqa()};
    aqa_builder.set<aqa::ASQS>(static_cast<std::uint32_t>(constants::AQ_DEPTH - 1));
    aqa_builder.set<aqa::ACQS>(static_cast<std::uint32_t>(constants::AQ_DEPTH - 1));
    regs_.aqa(aqa_builder.get());

    const std::uint64_t asqb = admin_queue_.queues.sq_iova();
    const std::uint64_t acqb = admin_queue_.queues.cq_iova();

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

    if (!enable()) {
        return std::unexpected{Error{
            .reason = "Controller did not become ready within CAP.TO after setting CC.EN",
            .severity = Error::Severity::ERROR
        }};
    }

    return {};
}

const QueuePair& Controller::admin_queues() const {
    return admin_queue_.queues;
}

const QueuePair* Controller::io_queue(QueueId qid) const {
    const auto it = std::ranges::find_if(io_queues_, [qid](const auto& ctx) {
        return ctx->queues.id().value == qid.value;
    });

    return it == io_queues_.end() ? nullptr : &(*it)->queues;
}

std::size_t Controller::io_queue_count() const {
    return io_queues_.size();
}

void Controller::set_state(ControllerState state) {
    state_ = std::move(state);
}

const std::optional<ControllerState>& Controller::state() const {
    return state_;
}

void Controller::grant_io_queues(std::uint16_t count) {
    io_qid_grant_ = count;
    io_queues_.reserve(count);
}

Result<QueueId> Controller::allocate_io_qid() {
    if (!free_io_qids_.empty()) {
        const QueueId qid{free_io_qids_.back()};
        free_io_qids_.pop_back();
        return qid;
    }

    if (next_io_qid_ > io_qid_grant_) {
        return std::unexpected{Error{
            .reason = std::format(
                "No IO queue IDs available ({} allowed max via Set Features Number of Queues)",
                io_qid_grant_
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    return QueueId{next_io_qid_++};
}

void Controller::release_io_qid(QueueId qid) {
    free_io_qids_.push_back(qid.value);
}

Result<QueueId> Controller::add_io_queue_pair(dma::DmaHeap& heap, std::size_t depth) {
    auto qid = allocate_io_qid();
    if (!qid) {
        return std::unexpected{qid.error()};
    }

    io_queues_.push_back(std::make_unique<QueueContext>(*qid, heap, regs_, depth));
    return *qid;
}

void Controller::remove_io_queue_pair(QueueId qid) {
    const auto it = std::ranges::find_if(io_queues_, [qid](const auto& ctx) {
        return ctx->queues.id().value == qid.value;
    });

    if (it == io_queues_.end()) {
        return;
    }

    io_queues_.erase(it);
    release_io_qid(qid);
}

QueueContext* Controller::queue_context(QueueId qid) {
    if (qid.value == constants::ADMIN_QID.value) {
        return &admin_queue_;
    }

    return io_queue_context(qid);
}

QueueContext* Controller::io_queue_context(QueueId qid) {
    const auto it = std::ranges::find_if(io_queues_, [qid](const auto& ctx) {
        return ctx->queues.id().value == qid.value;
    });

    return it == io_queues_.end() ? nullptr : it->get();
}

std::uint32_t Controller::max_io_queue_entries() const {
    return static_cast<std::uint32_t>(cap::MQES::extract(regs_.cap())) + 1;
}

bool Controller::requires_contiguous_queues() const {
    return cap::CQR::extract(regs_.cap()) == 1;
}

Result<wire::CompletionEntry> Controller::poll_command(QueueId qid, Cid cid) {
    QueueContext* ctx = queue_context(qid);
    if (ctx == nullptr) {
        return std::unexpected{Error{
            .reason = std::format("Polling on queue {}, which does not exist", qid.value),
            .severity = Error::Severity::ERROR
        }};
    }

    if (!ctx->in_flight.contains(cid)) {
        return std::unexpected{Error{
            .reason = std::format("Polling for CID {}, which is not in flight", cid),
            .severity = Error::Severity::ERROR
        }};
    }

    const auto timeout = qid.value == constants::ADMIN_QID.value ? constants::ADMIN_CMD_TIMEOUT
                                                                 : constants::IO_CMD_TIMEOUT;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (auto done = take_completion(*ctx, cid)) {
            return *done;
        }

        const std::optional<wire::CompletionEntry> completion = ctx->queues.cq().poll();
        if (!completion.has_value()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // NOTE: the command is still in flight so the cid should not be released
                return std::unexpected{Error{
                    .reason = std::format(
                        "Timed out after {}s waiting for CID {} on queue {}",
                        timeout.count(),
                        cid,
                        qid.value
                    ),
                    .severity = Error::Severity::ERROR
                }};
            }

            std::this_thread::yield();
            continue;
        }

        ctx->queues.sq().advance_head(completion->sq_head);
        if (auto diag = accept_completion(*ctx, *completion)) {
            return std::unexpected{std::move(*diag)};
        }
    }
}

std::optional<Result<wire::CompletionEntry>> Controller::try_poll_command(QueueId qid, Cid cid) {
    QueueContext* ctx = queue_context(qid);
    if (ctx == nullptr) {
        return std::unexpected{Error{
            .reason = std::format("Polling on queue {}, which does not exist", qid.value),
            .severity = Error::Severity::ERROR
        }};
    }

    if (!ctx->in_flight.contains(cid)) {
        return std::unexpected{Error{
            .reason = std::format("Polling for CID {}, which is not in flight", cid),
            .severity = Error::Severity::ERROR
        }};
    }

    while (const auto completion = ctx->queues.cq().poll()) {
        ctx->queues.sq().advance_head(completion->sq_head);
        if (auto diag = accept_completion(*ctx, *completion)) {
            return std::unexpected{std::move(*diag)};
        }
    }

    return take_completion(*ctx, cid);
}

void Controller::abandon_command(QueueId qid, Cid cid) {
    QueueContext* ctx = queue_context(qid);
    if (ctx == nullptr) {
        return;
    }

    const auto it = ctx->in_flight.find(cid);
    if (it == ctx->in_flight.end()) {
        return;
    }

    if (it->second.result.has_value()) {
        ctx->cids.release(it->second.cid);
        ctx->in_flight.erase(it);
    } else {
        it->second.abandoned = true;
    }
}

std::optional<Error>
Controller::accept_completion(QueueContext& ctx, const wire::CompletionEntry& completion) {
    const auto it = ctx.in_flight.find(Cid{completion.cid});
    if (it == ctx.in_flight.end()) {
        return Error{
            .reason =
                std::format("Completion for CID {}, which was never submitted", completion.cid),
            .severity = Error::Severity::ERROR
        };
    }

    InFlightCommand& entry = it->second;
    if (entry.abandoned) {
        // nobody is waiting on this result any more, might as well drop it and release the CID
        ctx.cids.release(entry.cid);
        ctx.in_flight.erase(it);
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

std::optional<wire::CompletionEntry> Controller::take_completion(QueueContext& ctx, Cid cid) {
    const auto it = ctx.in_flight.find(cid);
    if (it == ctx.in_flight.end() || !it->second.result.has_value()) {
        return std::nullopt;
    }

    const wire::CompletionEntry completion = *it->second.result;
    ctx.cids.release(it->second.cid);
    ctx.in_flight.erase(it);
    return completion;
}

bool Controller::rdy_is(unsigned want) {
    return csts::RDY::extract(regs_.csts()) == want;
}

void Controller::fetch_timeout() {
    const auto timeout_raw = cap::TIMEOUT::extract(regs_.cap());
    timeout_ = timeout_raw * 500ms;
}

bool Controller::change_controller_state_to(std::uint8_t en) {
    const std::uint32_t cc = regs_.cc();
    const auto current_en = static_cast<std::uint8_t>(cc::EN::extract(cc));

    if (!wait_for(
            [&] {
                return rdy_is(current_en);
            },
            timeout_
        )) {
        std::println("[TIMEOUT]: CSTS.RDY did not settle to {} before toggling CC.EN.", current_en);
        return false;
    }

    if (current_en != en) {
        regs_.cc(cc::EN::insert(cc, en));
    }

    if (!wait_for(
            [&] {
                return rdy_is(en);
            },
            timeout_
        )) {
        if (en == CtlDis) {
            std::println("[TIMEOUT]: Controller did not become disabled.");
        } else {
            std::println("[TIMEOUT]: Controller did not become enabled.");
        }
        return false;
    }

    if (en == CtlDis) {
        std::println("[INFO] Controller is disabled.");
    } else {
        std::println("[INFO] Controller is enabled.");
    }
    return true;
}

}  // namespace nvme
