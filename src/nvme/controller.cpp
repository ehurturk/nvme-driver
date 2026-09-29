#include "nvme/controller.hpp"

#include "nvme/queue.hpp"
#include "nvme/queue_entry.hpp"
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
#include <ranges>
#include <utility>

namespace nvme {

Controller::Controller(NvmeDeviceHandle device)
    : regs_{device.bar_region()},
      configuration_{ControllerConfiguration::select(regs_.cap(), device.dma_heap().geometry())},
      heap_{&device.dma_heap()},
      timeout_{125ms} {
    queues_.push_back(
        std::make_unique<Queue>(constants::ADMIN_QID, *heap_, regs_, constants::AQ_DEPTH, configuration_)
    );

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

const ControllerConfiguration& Controller::configuration() const {
    return configuration_;
}

Result<void> Controller::configure() {
    fetch_timeout();

    if (!disable()) {
        return std::unexpected{
            Error{.reason = "Controller did not become disabled within CAP.TO", .severity = Error::Severity::ERROR}
        };
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
    const auto ccMps = configuration_.cc_mps;

    const auto capMPSMax = cap::MPSMax::extract(cap);
    const auto capMPSMin = cap::MPSMin::extract(cap);
    const auto capMQES = cap::MQES::extract(cap);

    std::println("MPSMax: {}\nMPSMin: {}", capMPSMax, capMPSMin);
    std::println("Max Queue Entry Size (CAP.MQES): {}", capMQES);

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
            .reason = std::format("Controller supports no usable command set (CAP.CSS = {:#x})", capCss),
            .severity = Error::Severity::ERROR
        }};
    }

    // AQA, NOTE: NVMe controller expects 0s-based depth!!!!
    utils::DwordBuilder<std::uint32_t> aqa_builder{regs_.aqa()};
    aqa_builder.set<aqa::ASQS>(static_cast<std::uint32_t>(constants::AQ_DEPTH - 1));
    aqa_builder.set<aqa::ACQS>(static_cast<std::uint32_t>(constants::AQ_DEPTH - 1));
    regs_.aqa(aqa_builder.get());

    const std::uint64_t asqb = admin_queue().sq_iova();
    const std::uint64_t acqb = admin_queue().cq_iova();

    if (asqb % configuration_.nvme_page_size != 0 || acqb % configuration_.nvme_page_size != 0) {
        return std::unexpected{
            Error{.reason = "Admin queue bases do not satisfy CC.MPS alignment", .severity = Error::Severity::ERROR}
        };
    }

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

const Queue& Controller::admin_queue() const {
    return *queues_[constants::ADMIN_QID.value];
}

const Queue* Controller::io_queue(QueueId qid) const {
    if (qid.value == constants::ADMIN_QID.value || qid.value >= queues_.size()) {
        return nullptr;
    }

    return queues_[qid.value].get();
}

std::size_t Controller::io_queue_count() const {
    return static_cast<std::size_t>(std::ranges::count_if(queues_ | std::views::drop(1), [](const auto& queue) {
        return queue != nullptr;
    }));
}

void Controller::set_state(ControllerState state) {
    state_ = std::move(state);
}

const std::optional<ControllerState>& Controller::state() const {
    return state_;
}

void Controller::grant_io_queues(std::uint16_t count) {
    io_qid_grant_ = count;
    if (queues_.size() <= count) {
        queues_.resize(std::size_t{count} + 1);
    }
}

Result<QueueId> Controller::allocate_io_qid() {
    for (std::uint32_t qid = 1; qid <= io_qid_grant_; ++qid) {
        if (queues_[qid] == nullptr) {
            return QueueId{static_cast<std::uint16_t>(qid)};
        }
    }

    return std::unexpected{Error{
        .reason =
            std::format("No IO queue IDs available ({} allowed max via Set Features Number of Queues)", io_qid_grant_),
        .severity = Error::Severity::ERROR
    }};
}

Result<QueueId> Controller::add_io_queue_pair(std::size_t depth) {
    return allocate_io_qid().and_then([&](const QueueId& id) -> Result<QueueId> {
        queues_[id.value] = std::make_unique<Queue>(id, *heap_, regs_, depth, configuration_);
        return id;
    });
}

void Controller::remove_io_queue_pair(QueueId qid) {
    if (qid.value == constants::ADMIN_QID.value || qid.value >= queues_.size()) {
        return;
    }

    queues_[qid.value].reset();
}

Queue* Controller::queue(QueueId qid) {
    return qid.value < queues_.size() ? queues_[qid.value].get() : nullptr;
}

std::uint32_t Controller::max_io_queue_entries() const {
    return static_cast<std::uint32_t>(cap::MQES::extract(regs_.cap())) + 1;
}

bool Controller::requires_contiguous_queues() const {
    return cap::CQR::extract(regs_.cap()) == 1;
}

Result<wire::CompletionEntry> Controller::poll_command(QueueId qid, Cid cid) {
    Queue* q = queue(qid);
    if (q == nullptr) {
        return std::unexpected{Error{
            .reason = std::format("Polling on queue {}, which does not exist", qid.value),
            .severity = Error::Severity::ERROR
        }};
    }

    return q->wait(cid);
}

std::optional<Result<wire::CompletionEntry>> Controller::try_poll_command(QueueId qid, Cid cid) {
    Queue* q = queue(qid);
    if (q == nullptr) {
        return std::unexpected{Error{
            .reason = std::format("Polling on queue {}, which does not exist", qid.value),
            .severity = Error::Severity::ERROR
        }};
    }

    return q->poll(cid);
}

void Controller::abandon_command(QueueId qid, Cid cid, std::optional<dma::DmaBuffer> buffer) {
    if (Queue* q = queue(qid)) {
        q->abandon(cid, std::move(buffer));
    }
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
