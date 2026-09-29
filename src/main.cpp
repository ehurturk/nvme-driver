#include "dma/heap.hpp"
#include "nvme/admin/api.hpp"
#include "nvme/command.hpp"
#include "nvme/controller.hpp"
#include "nvme/controller_state.hpp"
#include "nvme/io/read.hpp"
#include "nvme/io/write.hpp"
#include "nvme/spec.hpp"
#include "os/mmio.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"
#include "vfio/group.hpp"

#include <linux/vfio.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <utility>

namespace {

const std::string NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int NVME_GROUP_ID = 6;

std::optional<nvme::ControllerState> execute_identify_controller_command(nvme::Controller& controller) {
    auto identify_result = nvme::admin::identify_controller(controller);

    if (!identify_result) {
        std::println("[ERROR]: {}", identify_result.error().reason);
        return std::nullopt;
    }

    nvme::ControllerState state{
        identify_result->mdts(),
        identify_result->cntlid(),
        identify_result->sqes(),
        identify_result->cqes(),
        identify_result->nn()
    };

    std::println("NVMe Properties:");
    std::println("\tVersion ID: {}", identify_result->vid());
    std::println("\tSerial Number: {}", identify_result->serial());
    std::println("\tModel Number: {}", identify_result->model().data());
    std::println("\tMaximum Data Transfer Size: {}", identify_result->mdts());
    std::println("\tController ID: {}", identify_result->cntlid());
    std::println("\tSubmission Queue Entry Size: {}", identify_result->sqes());
    std::println("\tCompletion Queue Entry Size: {}", identify_result->cqes());
    std::println("\tNumber of Namespaces: {}", identify_result->nn());

    return state;
}

void execute_identify_namespace_commands(nvme::Controller& controller, nvme::ControllerState& state) {
    auto nsids = nvme::admin::list_active_namespaces(controller);
    if (!nsids) {
        std::println("[ERROR]: {}", nsids.error().reason);
        return;
    }

    std::println("Active namespaces: {}", nsids->size());

    for (const std::uint32_t nsid : *nsids) {
        nvme::Result<nvme::admin::IdentifyNamespaceData> ns =
            nvme::admin::identify_namespace(controller, nsid);

        if (!ns) {
            std::println("[ERROR]: {}", ns.error().reason);
            continue;
        }

        std::println("Namespace {}:", nsid);
        std::println("\tSize (NSZE): {} LBAs", ns->nsze());
        std::println("\tCapacity (NCAP): {} LBAs", ns->ncap());
        std::println("\tIn use (NUSE): {} LBAs", ns->nuse());
        std::println("\tLBA size: {} bytes", ns->lba_size_bytes());
        std::println("\tMetadata size: {} bytes", ns->active_lba_format().metadata_size);

        state.add_namespace(
            nvme::NamespaceInfo{
                .nsid = nsid,
                .size = ns->nsze(),
                .capacity = ns->ncap(),
                .in_use = ns->nuse(),
                .lba_size_bytes = ns->lba_size_bytes(),
                .metadata_size = ns->active_lba_format().metadata_size
            }
        );
    }
}

void execute_read_logs_command(nvme::Controller& controller) {
    auto smart = nvme::admin::read_smart_log(controller);
    if (!smart) {
        std::println("[ERROR]: {}", smart.error().reason);
        return;
    }

    std::println(
        "SMART: temperature {} K, spare {}%, used {}%, read {} units, written {} units",
        smart->composite_temperature_kelvin(),
        smart->available_spare(),
        smart->percentage_used(),
        smart->data_units_read(),
        smart->data_units_written()
    );

    auto errors = nvme::admin::read_error_log(controller);
    if (!errors) {
        std::println("[ERROR]: {}", errors.error().reason);
        return;
    }

    std::println("Error log entries: {}", errors->size());
}

}  // namespace

int main() {
    namespace na = nvme::admin;

    try {
        vfio::VfioContainer container;
        vfio::VfioGroup group{std::format("/dev/vfio/{}", NVME_GROUP_ID), container};
        vfio::VfioDevice& device = group.get_device(NVME_PCI_ADDR);

        dma::DmaHeap dma_heap{container};

        // map BAR0 into address space of this driver's process so that we can access controller registers via MMIO
        os::MappedMemory bar0 = device.map_region(VFIO_PCI_BAR0_REGION_INDEX);

        nvme::Controller controller{nvme::NvmeDeviceHandle(os::MmioRegion{bar0.data()}, dma_heap)};

        if (auto configured = controller.configure(); !configured) {
            std::println("[ERROR]: {}", configured.error().reason);
            return EXIT_FAILURE;
        }

        auto state = execute_identify_controller_command(controller);
        if (!state) {
            return EXIT_FAILURE;
        }

        execute_identify_namespace_commands(controller, *state);
        controller.set_state(std::move(*state));

        execute_read_logs_command(controller);

        {
            auto allocated = na::set_number_of_queues(controller, nvme::constants::NR_IO_SQ, nvme::constants::NR_IO_CQ);

            if (!allocated) {
                std::println("[ERROR]: {}", allocated.error().reason);
                return EXIT_FAILURE;
            }

            std::println("# of IOSQs allocated: {}", allocated->allocated_sq);
            std::println("# of IOCQs allocated: {}", allocated->allocated_cq);
        }

        nvme::Result<nvme::QueueId> io_qid = na::create_io_queue_pair(controller, nvme::constants::IO_DEPTH);

        if (!io_qid) {
            std::println("[ERROR]: {}", io_qid.error().reason);
            return EXIT_FAILURE;
        }

        std::println("Created IO queue pair (qid {}, cqid {})", io_qid->value, io_qid->value);

        // write to namespace id 1 from starting LBA 0 of 4 LBAS
        {
            dma::DmaBuffer write_buf = dma_heap.alloc(512ull * 4, controller.configuration().nvme_page_size);

            // populate the write buffer
            std::array<std::byte, 2048> buf{};
            constexpr std::string_view msg = "hello nvme";

            std::ranges::transform(msg, buf.begin(), [](char c) {
                return static_cast<std::byte>(c);
            });

            std::memcpy(write_buf.uva(), buf.data(), buf.size());

            auto written = controller.execute(*io_qid, nvme::io::commands::Write{1, 0x0, 3}, std::move(write_buf));
            if (!written) {
                std::println("[ERROR]: {}", written.error().reason);
                return EXIT_FAILURE;
            }

            std::println("[INFO]: Wrote to the NVMe Namespace 1, 4 LBAs wide");
        }

        // read from namespace id 1 from starting LBA 0 and read 4 LBAs
        {
            auto read = controller.execute(
                *io_qid,
                nvme::io::commands::Read{1, 0x0, 3},
                dma_heap.alloc(512ull * 4, controller.configuration().nvme_page_size)
            );
            if (!read) {
                std::println("[ERROR]: {}", read.error().reason);
                return EXIT_FAILURE;
            }

            std::array<std::byte, 2048ull> res{};
            std::memcpy(res.data(), read->uva(), res.size());

            // hexdump the result
            utils::hexdump(res);
        }

    } catch (const std::exception& e) {
        std::println("[FATAL]: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
