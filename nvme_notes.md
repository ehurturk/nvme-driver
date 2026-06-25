# NVMe Notes Gathered From Base Spec + Transport Spec

## Data Structures

### Submission Queue Entry

Each "Common Command Format" command is **64 bytes** in size.

Common Command Format:

| Bytes  | Description 
----------------------
| 3 - 0  | CDW0: Common to all commands
| 7 - 4  | NSID: Specifies the namespace that this command applies to (if not used clear to 0h)
| 8 - 11 | CDW2: Command specific dword2
| 12 - 15 | CDW3: Command specific dword3
| 16 - 23 | MPTR (Metadata Pointer): If CWD0.PSDT is cleared to 00b, then this shall contain the address of contiguous physical buffer of metadata (dword aligned). If CDW0.PSDT is 0b10, then this hsall contain the address of an SGL segment that contains exactly 1 SGL Descriptor (dword aligned).
| 24 - 39 | DPTR (Data Pointer): Specifies the data used in the command. If CDW0.PSDT is 0b00, then bytes 24-31 are PRP Entry 1, and bytes 32-39 are PRP Entry 2 (refer to page 162 of base spec).
| 40 - 43 | CDW10: Command specific dword 10
| 44 - 47 | CDW11: Command specific dword 11
| 48 - 51 | CDW12: Command specific dword 12
| 52 - 55 | CDW13: Command specific dword 13
| 56 - 59 | CDW14: Command specific dword 14
| 60 - 63 | CDW15: Command specific dword 15


### Completion Queue Entry

"Common Completion Queue Entry Layout" is at least 16 bytes in size.

Common Completion Queue Entry Layout for Admin and all I/O command sets is as:

DW0: Command Specific
DW1: Command Specific
DW2: bits 0-15: SQ Head Pointer
     bits 16-31: SQ Identifier
DW3: bits 0-15: Command Identifier
     bit 16: Phase Bit
     bits 17-31: Status
     
DW2 is laid out as:

| Bits   | Description
======================
| 0 - 15 | SQ Head Pointer (SQHD): Indicates the current Submission Queue Head pointer for the SQ Indicated in the SQID field. This is used to indicate to the host the submission queue entries that have been consumed and may be re-used for new entries.
| 16 - 31 | SQ Identifier (SQID): Indicates the Submission Queue to which the associated command was issued. This is used by a host when 1+ SQs share a single CQ to uniquely determine the command completed in combination with the Command Identifier (CID).

DW3 is laid out as:

| Bits  | Description
======================
| 0 - 15 | Command Identifier (CID): Indicates the identifier of the command that is being completed. The identifier is assigned by a host when the command is submitted to the SQ. Combination of SQID + CID uniquely identifies the command that is being completed.
| 16     | Phase Bit (P): Indicates whether a completion queue entry is new. (Refer to section 4.2.4)
| 16 - 31 | Status: Indicates the status for the command that is being completed. 

#### Status Field

The Status field defines the status for the command indicates in the completion queue entry. A value of 0 indicates a successful command completion.

It is laid out as:

| Bits | Description
====================
| 17 - 24 | Status Code (SC)
| 25 - 27 | Status Code Type (SCT): Indicates the type of status code the controller is returning
| 28 - 29 | Command Retry Delay (CRD)
| 30      | More (M): If this is 1, then there is more status information for this command as part of the ERror Information log page that may be retrieved with the Get Log Page command.
| 31      | Do Not Retry (DNR): If this is 1, then it indicates that if the same command is re-submitted then it is expected to fail.

Refer to the spec for all information about status codes and their explanations. This is at section 4.2.3.1.

Command specific status codes are defined in section 4.2.3.2.

#### Phase Bit

Phase tag bit indicates whether a completion queue entry is new. 
> Phase tag bit for each completion queue entry in:
> - The ACQ shall be initialized to 0 by the host prior to setting CC.EN to 1 (?)
> - An I/O-CQ shall be initialized to 0 by the host prior to submitting the "Create I/O Completion Queue" command for that queue.

When the controller posts a new completion queue entry to the CQ, the controller shall invert the Phase bit in that CQE (for the host to detect the new CQE).

When a CQE is posted to a CQ slot in:
- The ACQ for the first time after CC.EN is set to 1, the P bit for that CQE is set to 1
- An I/O CQ for the first time after the "Create I/O Completion Queue" command completed for that queue, the P bit for that CQE is set to 1.

This behaviour continues for each CQE that is posted until the controller posts a CQE to the highest numbered CQ slot and wraps to CQ slot number 0.
- When this wrap occurs (due to the nature of the ring buffer), the P tag bit is then cleared to 0 in each CQE that is posted. This continues until another wrap occurs.

Each time a wrap occurs, the value of the P bit is inverted.



## PRP Entry Layout

This is referred to at section 4.3.1.

### SGL

This is referred to at section 4.3.2.



## Admin Command Set

Some important opcodes:

| Opcode | Command
===================
| 0x00   | Delete I/O Submission Queue
| 0x01   | Create I/O Submission Queue
| 0x02   | Get Log Page
| 0x04   | Delete I/O Completion Queue
| 0x05   | Create I/O Completion Queue


## How Does SPDK Represent Commands & Queue Entries?

The *Common Command Format* is represented as:

```c
struct spdk_nvme_cmd {
	/* dword 0 */
	uint16_t opc	:  8;	/* opcode */
	uint16_t fuse	:  2;	/* fused operation */
	uint16_t rsvd1	:  4;
	uint16_t psdt	:  2;
	uint16_t cid;		/* command identifier */

	/* dword 1 */
	uint32_t nsid;		/* namespace identifier */

	/* dword 2-3: command-specific (reserved by base spec) */
	uint32_t cdw2;
	uint32_t cdw3;

	/* dword 4-5 */
	uint64_t mptr;		/* metadata pointer */

	/* dword 6-9: data pointer */
	union {
		struct {
			uint64_t prp1;		/* prp entry 1 */
			uint64_t prp2;		/* prp entry 2 */
		} prp;

		struct spdk_nvme_sgl_descriptor sgl1;
	} dptr;

	/* command-specific */
	union {
		uint32_t cdw10;
		union spdk_nvme_cmd_cdw10 cdw10_bits;
	};
	/* command-specific */
	union {
		uint32_t cdw11;
		union spdk_nvme_cmd_cdw11 cdw11_bits;
	};
	/* command-specific */
	union {
		uint32_t cdw12;
		union spdk_nvme_cmd_cdw12 cdw12_bits;
	};
	/* command-specific */
	union {
		uint32_t cdw13;
		union spdk_nvme_cmd_cdw13 cdw13_bits;
	};
	/* dword 14-15 */
	uint32_t cdw14;		/* command-specific */
	uint32_t cdw15;		/* command-specific */
};
SPDK_STATIC_ASSERT(sizeof(struct spdk_nvme_cmd) == 64, "Incorrect size");
```

As can be seen from the layout of the struct, it is consisting of DWORD0 (opcode, fuse, psdt, cid), DWORD1 (nsid), DWORDs 2-3 (command-specific), DWORDS 4-5 for mptr, DWORDS 6-9 for the data pointer (either a prp entry struct or a scatter gather list descriptor). 

The rest of the struct has command-specific DWRDs, specifically CDW10 to CDW15. 

If we look closely at one of these DWRDs, say CDW10:

```c
/* command-specific */
union {
    uint32_t cdw10;
    union spdk_nvme_cmd_cdw10 cdw10_bits;
};
```

we can see that it is a 32-bit command descriptor. Since it is command-specific, it is represented
as a union. In the case of CDW10, it is represented as

```c
union spdk_nvme_cmd_cdw10 {
	uint32_t raw;
	struct {
		/* Controller or Namespace Structure */
		uint32_t cns       : 8;
		uint32_t reserved  : 8;
		/* Controller Identifier */
		uint32_t cntid     : 16;
	} identify;

	struct {
		/* Log Page Identifier */
		uint32_t lid       : 8;
		/* Log Specific Field */
		uint32_t lsp       : 7;
		/* Retain Asynchronous Event */
		uint32_t rae       : 1;
		/* Number of Dwords Lower */
		uint32_t numdl     : 16;
	} get_log_page;

	struct {
		/* Submission Queue Identifier */
		uint32_t sqid      : 16;
		/* Command Identifier */
		uint32_t cid       : 16;
	} abort;

	struct {
		/* NVMe Security Specific Field */
		uint32_t nssf      : 8;
		/* SP Specific 0 */
		uint32_t spsp0     : 8;
		/* SP Specific 1 */
		uint32_t spsp1     : 8;
		/* Security Protocol */
		uint32_t secp      : 8;
	} sec_send_recv;

	struct {
		/* Queue Identifier */
		uint32_t qid       : 16;
		/* Queue Size */
		uint32_t qsize     : 16;
	} create_io_q;

	struct {
		/* Queue Identifier */
		uint32_t qid       : 16;
		uint32_t reserved  : 16;
	} delete_io_q;
    
    // more commands...
};
SPDK_STATIC_ASSERT(sizeof(union spdk_nvme_cmd_cdw10) == 4, "Incorrect size");
```

Since this is a C-based API, it is represented as a union of admin commands where each command is a struct with bit-field members that are making up a 32-bit integer.


A Completion Queue Entry is represented as:
```c
/**
 * Completion queue entry
 */
struct spdk_nvme_cpl {
	/* dword 0 */
	uint32_t		cdw0;	/* command-specific */

	/* dword 1 */
	uint32_t		cdw1;	/* command-specific */

	/* dword 2 */
	uint16_t		sqhd;	/* submission queue head pointer */
	uint16_t		sqid;	/* submission queue identifier */

	/* dword 3 */
	uint16_t		cid;	/* command identifier */
	union {
		uint16_t                status_raw;
		struct spdk_nvme_status	status;
	};
};
SPDK_STATIC_ASSERT(sizeof(struct spdk_nvme_cpl) == 16, "Incorrect size");
```
