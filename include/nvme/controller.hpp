#pragma once

#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <print>
#include <thread>

#include "dma/heap.hpp"
#include "nvme/queue_pair.hpp"
#include "nvme/registers.hpp"
#include "nvme/spec.hpp"
#include "os/mmio.hpp"
#include "utils.hpp"

namespace nvme
{

using namespace std::chrono_literals;

struct NvmeDeviceHandle
{
    os::MmioRegion bar0;
    dma::DmaHeap&  dma;
};

class Controller final
{
  private:
    static constexpr std::uint8_t CtlEn  = 0b1;
    static constexpr std::uint8_t CtlDis = 0b0;

  public:
    explicit Controller(NvmeDeviceHandle device)
            : regs_{device.bar0}, admin_queue_pair_{device.dma, regs_.doorbells(AdminQueuePair::AQ_ID)},
              timeout_{125ms}
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
};

} // namespace nvme
