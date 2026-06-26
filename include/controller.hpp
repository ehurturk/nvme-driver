#pragma once

#include <print>
#include <chrono>
#include <thread>
#include <assert.h>

#include "nvme_spec.hpp"

namespace nvme
{

using namespace std::chrono_literals;
class Controller final
{
  private:
    static constexpr std::uint8_t CtlEn = 0b1;
    static constexpr std::uint8_t CtlDis = 0b0;

  public:
    // bar0 must be a *mmap*ed memory of the nvme device
    Controller(void *bar0) : ctl_space_{bar0}, timeout_{125ms}
    {
        auto vs = ctl_space_.vs();
        auto minor = vs::MinorVersion::extract(vs);
        auto major = vs::MajorVersion::extract(vs);

        std::println("[INFO]: NVMe Version {}.{}", major, minor);
    }

    ~Controller()
    {
        disable();
    }

    // Enable the controller
    void enable()
    {
        change_controller_state_to(CtlEn);
    }

    // Disable the controller
    void disable()
    {
        change_controller_state_to(CtlDis);
    }

    void configure()
    {
        fetch_timeout();
        disable();

        std::uint8_t ccAms{0};

        auto capAms = cap::AMS::extract(ctl_space_.cap());

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
        uint8_t ccMps{0};

        auto capMPSMax = cap::MPSMax::extract(ctl_space_.cap());
        auto capMPSMin = cap::MPSMin::extract(ctl_space_.cap());

        std::println("MPSMax: {}\nMPSMin: {}", capMPSMax, capMPSMin);

        if (ccMps < capMPSMin || ccMps > capMPSMax) {
            ccMps = capMPSMin;
            std::println("[INFO]: ccMps {} is out of bounds, setting it as "
                         "capMPSMin: {}",
                    ccMps, capMPSMin);
        }

        // Set CC.CSS
        uint8_t ccCss{0};

        auto capCss = cap::CSS::extract(ctl_space_.cap());

        if (capCss & cap::css::NCSS_BIT) {
            std::println("NCSS Bit is present...");
            ccCss = cc::css::NCSS;
        } else {
            std::println("NVME Command Set is not found.");
            return;
        }

        // Insert AS, MPS, CSS into CC
        auto cc = cc::AMS::insert(ctl_space_.cc(), ccAms);
        cc = cc::MPS::insert(ctl_space_.cc(), ccMps);
        cc = cc::CSS::insert(ctl_space_.cc(), ccCss);

        // Set CC.IOSQES / CC.IOCQES
        // For creating the admin queues, I will set IOSQES/IOCQES to standard
        // NVM Command Set values (64 and 16 bytes respectively), and after running
        // Identify Controller command, I will adjust these to the correct required
        // minimums to create the I/O queues.
        cc = cc::IOSQES::insert(ctl_space_.cc(), constants::IO_SQES);
        cc = cc::IOCQES::insert(ctl_space_.cc(), constants::IO_CQES);

        // Setup AQA & ASQ & ACQ

        volatile std::uint32_t aqa = ctl_space_.aqa();
        aqa = nvme::aqa::ASQS::insert(ctl_space_.aqa(), constants::AQ_DEPTH);
        aqa = nvme::aqa::ACQS::insert(ctl_space_.aqa(), constants::AQ_DEPTH);

        // TODO: Populate adminQueue queues with IOVA and UVA.
        nvme::AdminQueuePair adminPair{};

        volatile std::uint64_t asq = ctl_space_.asq();
        std::uint64_t asqb = 0;
        assert(asqb % (1 << ccMps) == 0 &&
                "ASQB shall be memory page aligned (based on the value in "
                "CC.MPS)");
        asq = nvme::asq::ASQB::insert(asq, asqb);

        volatile uint64_t acq = ctl_space_.acq();
        std::uint64_t acqb = 0;
        assert(acqb % (1 << ccMps) == 0 &&
                "ACQB shall be memory page aligned (based on the value in "
                "CC.MPS)");
        acq = nvme::acq::ACQB::insert(acq, acqb);

        // TODO: should the cc values be written before AQA, ACQ, ASQ be written, or
        //       does the order doesn't matter?
        ctl_space_.cc(cc); // is this needed? we write to cc in enable() anyways?

        // write the AQA, ACQ, ASQ attributes before enabling the controller
        ctl_space_.aqa(aqa);
        ctl_space_.acq(acq);
        ctl_space_.asq(asq);
    }

  private:
    bool wait_for(
            std::predicate auto &&predicate, std::chrono::milliseconds timeout)
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
        return csts::RDY::extract(ctl_space_.csts()) == want;
    }

    void fetch_timeout()
    {
        auto timeout_raw = cap::TIMEOUT::extract(ctl_space_.cap());
        timeout_ = timeout_raw * 500ms;
    }

    // en = 0b0 -> disable
    // en = 0b1 -> enable
    void change_controller_state_to(std::uint8_t en)
    {
        auto cc = cc::EN::insert(ctl_space_.cc(), en);
        ctl_space_.cc(cc);

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
    NVMeBar0 ctl_space_;
    std::chrono::milliseconds timeout_;
};

} // namespace nvme
