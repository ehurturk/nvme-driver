#include <linux/vfio.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <print>
#include <string>

#include "dma/heap.hpp"
#include "nvme/admin/commands.hpp"
#include "nvme/admin/features.hpp"
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

        // vfio-pci leaves the Bus Master Enabled (BME) bit in the Command register of a PCIe device's configuration
        // space turned off by default, therefore this has to be manually turned on for the device to perform DMA.
        device.enable_bus_master();

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
        std::optional<nvme::CompletionEntry> completion;
        while (true) {
            completion = acq.poll();
            if (completion.has_value())
                break;
        }

        nvme::CompletionEntry& value = *completion;
        asq.advance_head(value.sq_head);
        std::println("Identify command completion status: {}", value.stat());
        std::println("Identify command sqid and cid: {}, {}", value.sqid, value.cid);

        auto* identify_result =
            compat::load_object_from_bytes<nvme::admin::IdentifyControllerData>(result_buffer.uva());

        std::println("NVMe Properties:");
        std::println("\tVersion ID: {}", identify_result->vid());
        std::println("\tSerial Number: {}", identify_result->serial().data());
        std::println("\tModel Number: {}", identify_result->model().data());
        std::println("\tMaximum Data Transfer Size: {}", identify_result->mdts());
        std::println("\tController ID: {}", identify_result->cntlid());
        std::println("\tSubmission Queue Entry Size: {}", identify_result->sqes());
        std::println("\tCompletion Queue Entry Size: {}", identify_result->cqes());
        std::println("\tNumber of Namespaces: {}", identify_result->nn());


        // now we have necessary information on creating IO queues.
        // let's read the documentation on how to create IO queues.
        // TODO: log CAP.CQR
        //
        //  1) Create the IO Completion queue with the Create IO Completion Queue Command
        //  2) To create the IO submission queue, host should build: Create IO Submission Queue command. Attributes of
        //  that command should be:
        //      - CDW0.OPC = 0x1;
        //      - CDW0.FUSE = 0x0;
        //      - CDW0.CID = allocate cid;
        //      - NSID = 0;
        //      - MPTR = 0; (no metadata pointers are used for this command);
        //      - DPTR.PRP1 = address of PRP list
        //      - DPTR.PRP2 = 0x0;
        //      - CDW10.QSIZE = # of queue to create (0s based!) should be <= CAP.MQES
        //      - CDW10.QID = SQ ID;
        //      - CDW11.CQID = IO Completion Queue to post completion queues
        //      - CDW11.QPRIO = 0b10 (medium priority)
        //      - CDW11.PC = 0x0 (PRP1 buffer is not contiguous)
        constexpr int number_of_iosqs = 4;
        constexpr int number_of_iocqs = 4;

        static_assert(number_of_iosqs >= 1 && number_of_iosqs < 65535);
        static_assert(number_of_iocqs >= 1 && number_of_iocqs < 65535);

        // Send a SetFeatures Command with Number of Queues attribute
        nac::SetFeatures<nvme::features::NumberOfQueues> set_features_cmd_descriptor{};
        set_features_cmd_descriptor.feature.ncq = number_of_iocqs;
        set_features_cmd_descriptor.feature.nsq = number_of_iosqs;

        nvme::CommonCommand set_features_cmd = set_features_cmd_descriptor.build(aq_cid_allocator.allocate());
        asq.push(set_features_cmd);

        std::optional<nvme::CompletionEntry> set_features_comp;
        while (true) {
            set_features_comp = acq.poll();
            if (set_features_comp.has_value())
                break;
        }

        asq.advance_head(set_features_comp->sq_head);

        auto set_features_res = nac::SetFeatures<nvme::features::NumberOfQueues>::decode_completion(*set_features_comp);
        std::println("Create IOCQ status: {}", set_features_comp->stat());
        std::println("# of IOSQs allocated: {}", set_features_res.allocated_sq);
        std::println("# of IOCQs allocated: {}", set_features_res.allocated_cq);

        //==================================================================
        // Create a Create IO Completion Queue Command
        dma::DmaBuffer     iocq_buf = dma_heap.alloc(number_of_iocqs * sizeof(nvme::CompletionEntry));

        nac::CreateIoCq create_iocq_cmd_desc{};
        create_iocq_cmd_desc.prp1 = iocq_buf.iova();
        create_iocq_cmd_desc.qid  = 1; // iocq id
        create_iocq_cmd_desc.qsize = nvme::constants::IO_DEPTH;
        create_iocq_cmd_desc.interrupts_enabled = false;
        create_iocq_cmd_desc.physically_contiguous = true;
        create_iocq_cmd_desc.interrupt_vector      = 0;

        nvme::CommonCommand create_iocq_cmd = create_iocq_cmd_desc.build(aq_cid_allocator.allocate());

        asq.push(create_iocq_cmd);

        // busy spin and poll for completion
        std::optional<nvme::CompletionEntry> ciocq_comp;
        while (true) {
            ciocq_comp = acq.poll();
            if (ciocq_comp.has_value())
                break;
        }

        asq.advance_head(ciocq_comp->sq_head);
        std::println("Create IOCQ status: {}", ciocq_comp->stat());

        // ======================================================================
        // TODO: determine the correct size for this?
        dma::DmaBuffer iosq_buf = dma_heap.alloc(number_of_iosqs * sizeof(nvme::CommonCommand));

        nac::CreateIoSq create_iosq_cmd_desc{};
        create_iosq_cmd_desc.physically_contiguous = true;
        create_iosq_cmd_desc.qid                   = 2;
        create_iosq_cmd_desc.qsize                 = nvme::constants::IO_DEPTH - 1;
        create_iosq_cmd_desc.prp1                  = iosq_buf.iova();
        create_iosq_cmd_desc.cqid = 1;

        nvme::CommonCommand create_iosq_cmd = create_iocq_cmd_desc.build(aq_cid_allocator.allocate());

        asq.push(create_iosq_cmd);

        std::optional<nvme::CompletionEntry> ciosq_comp;
        while (true) {
            ciosq_comp = acq.poll();
            if (ciosq_comp.has_value()) break;
        }

        asq.advance_head(ciosq_comp->sq_head);

        std::println("Create IOSQ status: {}", ciosq_comp->stat());

        std::println("=========");
        std::println("Final ASQ head: {}", asq.head());
        std::println("Final ASQ tail: {}", asq.tail());
        std::println("\nFinal ACQ tail: {}", acq.head());

        // ISSUES:
        // - NOTE: Have to set a Set Features command before creating the IO queues with the
        //         Number of Queues attribute set to the requested number of IOSQs and IOCQs.
        //         Completion Queue Entry for this specifies the # of queues allocated by controller.
        // - NOTE: Check CAP.MQES (max no of entries) and CAP.CQR (queues are required to be phys. continuous)
        // - TODO: put CompletionEntry and CommonCommand into a nvme::entry::SQEntry and nvme::entry::CQEntry type of
        //         common namespace
        // - TODO: ALWAYS CHECK AGAINS CAP.MQES!!!!!
        // 1) CAP.MQES = # of max queu entries, in QEMU it is 2048.
        // 2) Linux uses 1024 for IO Queue entry size.
        // 3) 1024 entries => 1024 * sizeof(CommonCommand) = 1024 * 64 = 65536 bytes
        // 4) Therefore, the IO Queue needs 65536 bytes of DMA-able host memory.
        // 5) If physically_contiguous is set to true, then we have to ensure that the allocator allocates
        //    uva pages that have contiguous IOVA mappings
        // 6) If we can't guarantee it, we can use a PRP list.
    } catch (const std::exception& e) {
        std::println("[FATAL]: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
