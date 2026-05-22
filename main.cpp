#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

struct pci_resource 
{
    uint8_t* ptr;
    size_t len;
};

/*
 * Open the pci device at address `pci_addr`, map the file into
 * the memory, and return the mapped address pointer (ptr) and
 * length (len).
 */
struct pci_resource map_pci_resource(const char* pci_addr) {
    struct pci_resource res;

    char path[128];
    snprintf(path, 128, "/sys/bus/pci/devices/%s/resource0", pci_addr);

    int nvm_res_fd = open(path, O_RDWR);

    struct stat st;

    if (fstat(nvm_res_fd, &st) == -1) {
        perror("fstat");
        close(nvm_res_fd);
        exit(-1);
    }

    res.len = st.st_size;

    if (res.len == 0) {
        fprintf(stderr, "pci addr has length 0");
        exit(-1);
    }

    res.ptr = (uint8_t*) mmap(NULL, res.len, PROT_READ | PROT_WRITE, MAP_SHARED, nvm_res_fd, 0);

    if (res.ptr == NULL) {
        fprintf(stderr, "pci mapping failed");
        exit(-1);
    }

    return res;
}

/* We use DMA to enable the transfer of data between the
 * host system and the NVMe device.
 * 
 * We initialize the DMA memory for all submission and
 * completion queues, as well as buffers that the device
 * can read from and write to.
 */

int main()
{
    // TODO:
    // Our task: read one block from the NVMe device and print it.
    //    1) Open the NVMe device
    //    2) Map BAR0 to the address space of the driver
    //    3) Configure & reset the controller
    //    4) Setup admin queue
    //    5) Create I/O queue via sending admin commands
    //    6) Submit an I/O command to an I/O queue
    
    // 1) Open the main VFIO interface to create a new IOMMU container
    int vfio_fd = open("/dev/vfio/vfio", O_RDWR);


}
