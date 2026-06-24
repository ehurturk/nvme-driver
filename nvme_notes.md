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
