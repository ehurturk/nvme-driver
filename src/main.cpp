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
#include "nvme/queue_entry.hpp"
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

        {
            // allocate and map 4 KB result buffer to store the result
            dma::DmaBuffer result_buffer = dma_heap.alloc(4096);

            std::expected<nvme::CompletionEntry, nvme::ControllerDiagnostic> identify =
                    controller.execute_admin_command<nac::IdentifyControllerType>(
                            [&result_buffer](nac::IdentifyControllerType& item) {
                                item.csi  = nac::IdentifyControllerType::Csi::NVM;
                                item.prp1 = result_buffer.iova();
                            });

            if (!identify) {
                std::println("[ERROR]: {}", identify.error().reason);
                return EXIT_FAILURE;
            }

            std::println("Identify command completion status: {}", identify->stat());
            std::println("Identify command sqid and cid: {}, {}", identify->sqid, identify->cid);

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
        }

        //  1) Create the IO Completion queue with the Create IO Completion Queue Command
        //  2) To create the IO submission queue, host should build: Create IO Submission Queue command. Attributes of
        //  that command should be:
        //      - CDW0.FUSE = 0x0;
        //      - NSID = 0;
        //      - MPTR = 0; (no metadata pointers are used for this command);
        //      - DPTR.PRP1 = address of PRP list
        //      - DPTR.PRP2 = 0x0;
        //      - CDW10.QSIZE = # of queue to create (0s based!) should be <= CAP.MQES
        //      - CDW10.QID = SQ ID;
        //      - CDW11.CQID = IO Completion Queue to post completion queues
        //      - CDW11.QPRIO = 0b10 (medium priority)
        //      - CDW11.PC = 0x0 (PRP1 buffer is not contiguous) BUG: are we sure about this?
        {
            constexpr int number_of_iosqs = 64;
            constexpr int number_of_iocqs = 64;

            static_assert(number_of_iosqs >= 1 && number_of_iosqs < 65535);
            static_assert(number_of_iocqs >= 1 && number_of_iocqs < 65535);

            using SetNoQueues = nac::SetFeatures<nvme::features::NumberOfQueues>;
            std::expected<nvme::CompletionEntry, nvme::ControllerDiagnostic> set_features =
                    controller.execute_admin_command<SetNoQueues>([](SetNoQueues& item) {
                        item.feature.ncq = number_of_iocqs;
                        item.feature.nsq = number_of_iosqs;
                    });

            if (!set_features) {
                std::println("[ERROR]: {}", set_features.error().reason);
                return EXIT_FAILURE;
            }

            auto set_features_res = nac::SetFeatures<nvme::features::NumberOfQueues>::decode_completion(*set_features);
            std::println("Create IOCQ status: {}", set_features->stat());
            std::println("# of IOSQs allocated: {}", set_features_res.allocated_sq);
            std::println("# of IOCQs allocated: {}", set_features_res.allocated_cq);
        }

        {
            // Create a Create IO Completion Queue Command
            dma::DmaBuffer iocq_buf = dma_heap.alloc(nvme::constants::IO_DEPTH * sizeof(nvme::CompletionEntry));
            std::expected<nvme::CompletionEntry, nvme::ControllerDiagnostic> create_iocq =
                    controller.execute_admin_command<nac::CreateIoCq>([&iocq_buf](nac::CreateIoCq& item) {
                        item.prp1                  = iocq_buf.iova();
                        item.qid                   = 1;
                        item.qsize                 = nvme::constants::IO_DEPTH;
                        item.interrupts_enabled    = false;
                        item.physically_contiguous = true;
                        item.interrupt_vector      = 0;
                    });
            if (!create_iocq) {
                std::println("[ERROR]: {}", create_iocq.error().reason);
                return EXIT_FAILURE;
            }
            std::println("Create IOCQ status: {}", create_iocq->stat());
        }

        {
            // Create a Create IO Submission Queue Command

            // 1024 * 64 = 65536
            // this occupies 16 pages
            // therefore should use prp lists
            // check CAP.CQR
            dma::DmaBuffer iosq_buf = dma_heap.alloc(nvme::constants::IO_DEPTH * sizeof(nvme::CommonCommand));
            std::expected<nvme::CompletionEntry, nvme::ControllerDiagnostic> create_iosq =
                    controller.execute_admin_command<nac::CreateIoSq>([&iosq_buf](nac::CreateIoSq& item) {
                        item.physically_contiguous = true;
                        item.qid                   = 2;
                        item.qsize                 = nvme::constants::IO_DEPTH - 1;
                        item.prp1                  = iosq_buf.iova();
                        item.cqid                  = 1;
                    });

            if (!create_iosq) {
                std::println("[ERROR]: {}", create_iosq.error().reason);
                return EXIT_FAILURE;
            }

            std::println("Create IOSQ status: {}", create_iosq->stat());
            std::println("=========");
        }

        auto& aq  = controller.admin_queues();
        auto& asq = aq.sq();
        auto& acq = aq.cq();

        std::println("Final ASQ head: {}", asq.head());
        std::println("Final ASQ tail: {}", asq.tail());
        std::println("\nFinal ACQ tail: {}", acq.head());
    } catch (const std::exception& e) {
        std::println("[FATAL]: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
