#include <linux/vfio.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <print>
#include <string>

#include "dma/heap.hpp"
#include "nvme/admin/commands.hpp"
#include "nvme/admin/identify.hpp"
#include "nvme/cid.hpp"
#include "nvme/controller.hpp"
#include "nvme/spec.hpp"
#include "os/mmio.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"
#include "vfio/group.hpp"

namespace
{
const std::string NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int     NVME_GROUP_ID = 6;
} // namespace

int main()
{
    namespace na  = nvme::admin;
    namespace nac = na::commands;

    try {
        vfio::VfioContainer container;
        vfio::VfioGroup     group{std::format("/dev/vfio/{}", NVME_GROUP_ID), container};
        vfio::VfioDevice    device = group.get_device(NVME_PCI_ADDR);

        dma::DmaHeap dma_heap{container};

        os::MappedMemory bar0 = device.map_region(VFIO_PCI_BAR0_REGION_INDEX);

        nvme::Controller controller{{os::MmioRegion{bar0.data()}, dma_heap}};

        controller.configure();

        // Create a command:
        nvme::CidAllocator aq_cid_allocator{nvme::constants::AQ_DEPTH};
        std::uint16_t      first_cmd_id = aq_cid_allocator.allocate();

        nac::IdentifyControllerType identify{};

        // allocate and map 4 KB result buffer to store the result
        dma::DmaBuffer result_buffer = dma_heap.alloc(4096);

        identify.prp1 = result_buffer.iova();
        identify.csi  = nac::IdentifyControllerType::Csi::NVM;

        nvme::CommonCommand cmd = identify.build(first_cmd_id);
        auto&               aqs = controller.admin_queues();
        auto&               asq = aqs.sq();
        auto&               acq = aqs.cq();

        asq.push(cmd);

        // busy spin and poll for completion
        // BUG: infinite loops! GDB time...
        std::optional<nvme::CompletionEntry> completion;
        while (true) {
            completion = acq.poll();
            if (completion.has_value())
                break;
        }

        nvme::CompletionEntry& value = *completion;
        asq.advance_head(value.sq_head);
        std::print("Identify command succeeded: {}", value.succeeded());
        std::print("Identify command sqid and cid: {}, {}", value.sqid, value.cid);

    } catch (const std::exception& e) {
        std::println("[FATAL]: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
