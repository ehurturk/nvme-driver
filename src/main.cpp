#include "dma/heap.hpp"
#include "nvme/admin/api.hpp"
#include "nvme/command.hpp"
#include "nvme/controller.hpp"
#include "nvme/controller_state.hpp"
#include "nvme/queue_pair.hpp"
#include "nvme/spec.hpp"
#include "os/mmio.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"
#include "vfio/group.hpp"

#include <linux/vfio.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <optional>
#include <print>
#include <string>

namespace {

const std::string NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int NVME_GROUP_ID = 6;

std::optional<nvme::ControllerState>
execute_identify_controller_command(nvme::Controller& controller, dma::DmaHeap& heap) {
    auto identify_result = nvme::admin::identify_controller(controller, heap);

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

void execute_identify_namespace_commands(
    nvme::Controller& controller,
    dma::DmaHeap& dma_heap,
    nvme::ControllerState& state
) {
    auto nsids = nvme::admin::list_active_namespaces(controller, dma_heap);
    if (!nsids) {
        std::println("[ERROR]: {}", nsids.error().reason);
        return;
    }

    std::println("Active namespaces: {}", nsids->size());

    for (const std::uint32_t nsid : *nsids) {
        nvme::Result<nvme::admin::IdentifyNamespaceData> ns =
            nvme::admin::identify_namespace(controller, dma_heap, nsid);

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

void execute_read_logs_command(nvme::Controller& controller, dma::DmaHeap& dma_heap) {
    auto smart = nvme::admin::read_smart_log(controller, dma_heap);
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

    auto errors = nvme::admin::read_error_log(controller, dma_heap);
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

        os::MappedMemory bar0 = device.map_region(VFIO_PCI_BAR0_REGION_INDEX);

        nvme::Controller controller{nvme::NvmeDeviceHandle(os::MmioRegion{bar0.data()}, dma_heap)};

        if (auto configured = controller.configure(); !configured) {
            std::println("[ERROR]: {}", configured.error().reason);
            return EXIT_FAILURE;
        }

        auto state = execute_identify_controller_command(controller, dma_heap);
        if (!state) {
            return EXIT_FAILURE;
        }

        execute_identify_namespace_commands(controller, dma_heap, *state);
        controller.set_state(std::move(*state));

        execute_read_logs_command(controller, dma_heap);

        {
            auto allocated = na::set_number_of_queues(
                controller,
                nvme::constants::NR_IO_SQ,
                nvme::constants::NR_IO_CQ
            );

            if (!allocated) {
                std::println("[ERROR]: {}", allocated.error().reason);
                return EXIT_FAILURE;
            }

            std::println("# of IOSQs allocated: {}", allocated->allocated_sq);
            std::println("# of IOCQs allocated: {}", allocated->allocated_cq);
        }

        nvme::Result<nvme::QueuePair> io_qpair =
            na::create_io_queue_pair(controller, dma_heap, nvme::constants::IO_DEPTH);

        if (!io_qpair) {
            std::println("[ERROR]: {}", io_qpair.error().reason);
            return EXIT_FAILURE;
        }

        std::println(
            "Created IO queue pair (qid {}, cqid {})",
            io_qpair->id().value,
            io_qpair->id().value
        );

    } catch (const std::exception& e) {
        std::println("[FATAL]: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
