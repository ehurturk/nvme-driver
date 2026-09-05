# Roadmap

I decided to write these roadmap milestones a bit late, thats why there are no entries for initial parts of this project (i.e. QEMU guest VM setup, Linux VFIO setup, NVME Controller initialization driver, etc.)

## Creation Admin Queues! (18-19 July 2026)
TODO

### Creating IO Queues! (22 July 2026)

I set on to create IO submission and completion queues using the `Create IO Submission Queue` and `Create IO Completion Queue` commands.

I realized that I have to specify the number of queues that the driver will use to the controller prior to creating these queues, as I constantly got failure status for `Create IO Submission Queues` command:

```
Set Number of Queues Features status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
Number of IOSQs allowed: 64
Number of IOCQs allowed: 64
Create IOCQ status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
Create IOSQ status: raw=0x8203, succeeded=false, phase=1, sct=1, sc=1, crd=0, more=false, dnr=true
```

For the IOSQ status, a `Status Code Type (SCT)` of 1 means it is a Command error, and `Status Code (SC)` for Command errors mean `Invalid Queue Identifier`. Upon looking at QEMU's implementation of an NVME controller, I found out:

```c
// in nvme_create_sq():
if (unlikely(!sqid || sqid > n->conf_ioqpairs || n->sq[sqid] != NULL)) {
    trace_pci_nvme_err_invalid_create_sq_sqid(sqid);
    return NVME_INVALID_QID | NVME_DNR;
}
```

in `nvme_create_sq()`. Therefore, the `NVME_INVALID_QID` status code I was getting was probably due to the fact that `sqid > n->conf_ioqpairs`.

Or was it?

I have opened GDB and hex dumped the command right before submitting it to the admin submission queue:

```
Breakpoint 1, main () at /root/nvme_driver/src/main.cpp:180
180	        nvme::CommonCommand create_iosq_cmd = create_iocq_cmd_desc.build(aq_cid_allocator.allocate());
(gdb) n
182	        asq.push(create_iosq_cmd);
(gdb) x/16xw &create_iosq_cmd
0x7fffffffe700:	0x00030005	0x00000000	0x00000000	0x00000000
0x7fffffffe710:	0x00000000	0x00000000	0x00004000	0x00000000
0x7fffffffe720:	0x00000000	0x00000000	0x03ff0001	0x00000001
0x7fffffffe730:	0x00000000	0x00000000	0x00000000	0x00000000
```

There are strange things here:

1) CDW0 (0,0 element) is 0x00030005. If we look at what CDW0 represents as bit fields:
```c++
using OPC  = utils::BitField<DWRD, 0, 8>;
using FUSE = utils::BitField<DWRD, 8, 2>;
using PSDT = utils::BitField<DWRD, 14, 2>;
using CID  = utils::BitField<DWRD, 16, 16>;
```
we can see that opcode is the first 8 bits. So, opcode is 5 apparently. But, this is a Create IO **Submission**Queue command, which has opcode 1?

Which command has opcode 5? Create IO **Completion** command has it. Why did I build a completion queue command then?

OOPS, typo: I was creating my command for create submission queue using:
```c++
nvme::CommonCommand create_iosq_cmd = create_iocq_cmd_desc.build(aq_cid_allocator.allocate());
```

which as you can see builds a create io completion queue command. Changing that c to s resulted in:
```
Set Number of Queues Features status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
Number of IOSQs allocated: 64
Number of IOCQs allocated: 64
Create IOCQ status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
Create IOSQ status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
```
Yesssir.

On a side note, my payloads for the Set Number of Queues Features were `nsq=4, ncq=4`, whereas my completion entry output `NUmber of IOSQs allocated: 64`. Why tho?

Digging though QEMU's NVME Set Features implementation revealed this:

```c
// in nvme_set_features():
case NVME_NUMBER_OF_QUEUES:
    // ...
    req->cqe.result = cpu_to_le32((n->conf_ioqpairs - 1) |
                                    ((n->conf_ioqpairs - 1) << 16));
    break;
```

I have found this code in initialization process of the QEMU NVME controller as well:

```c
// in nvme_init_state():
n->conf_ioqpairs = n->params.max_ioqpairs;
```

And I realized the max number of io queues can be set as QEMU flags, and if it is not set it is defualted to 64.

So the reason I got 64 as number of IO queues allowed was this, QEMU just writes the default max number of io queues that it is allowed to use as completion queue entry. 

This confused me a lil because the NVME spec says:
> The completion queue entry for this Set Features command indicates the number of I/O Submission Queues and I/O Completion
> Queues allocated by the controller.

So I thought it should return 4, but it is what it is.

Final output:

```
[INFO]: NVMe Version 1.4
[INFO] Controller is disabled.
AMS: Vendor specific.
MPSMax: 4
MPSMin: 0
Max Queue Entry Size (CAP.MQES): 2047
NCSS Bit is present...
[INFO] Controller is enabled.
Identify command completion status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
Identify command sqid and cid: 0, 0
NVMe Properties:
	Version ID: 6966
	Serial Number: deadbeef            QEMU NVMe Ctrl                          10.2.2
	Model Number: QEMU NVMe Ctrl                          10.2.2
	Maximum Data Transfer Size: 7
	Controller ID: 0
	Submission Queue Entry Size: 102
	Completion Queue Entry Size: 68
	Number of Namespaces: 256
Create IOCQ status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
# of IOSQs allocated: 64
# of IOCQs allocated: 64
Create IOCQ status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
Create IOSQ status: raw=0x0001, succeeded=true, phase=1, sct=0, sc=0, crd=0, more=false, dnr=false
=========
Final ASQ head: 4
Final ASQ tail: 4

Final ACQ tail: 4
[INFO] Controller is disabled.
```

## IO Commands

### Read & Write commands (5 September 2026)

I have found out that some of the IO commands are not fully described in the Base spec, thus I had to dig out the NVM command set spec and look at the common IO commands from there.

Read and write commands were easy to implement as they have a trivial state (just the starting LBA and number of blocks to read + the namespace id to read from).

I think I spent the most time here learning about namespaces and logical block addresses.

Anyways, I have setup a simple write & read test as follows:

```c++
// write to namespace id 1 from starting LBA 0 of 4 LBAS
{
	dma::DmaBuffer write_buf = dma_heap.alloc(512ull * 4);

	// populate the write buffer
	std::array<std::byte, 2048> buf{};
	constexpr std::string_view msg = "hello nvme";

	std::ranges::transform(msg, buf.begin(), [](char c) {
		return static_cast<std::byte>(c);
	});

	std::memcpy(write_buf.uva(), buf.data(), buf.size());

	const nvme::io::commands::Write write_cmd {dma::DmaView{write_buf}, 1, 0x0, 4};

	auto done = controller.execute_io_command(*io_qid, write_cmd);
	if (!done) {
		std::println("[ERROR]: {}", done.error().reason);
		return EXIT_FAILURE;
	}

	if (!done->ok()) {
		std::println("[ERROR]: Command Error");
		return EXIT_FAILURE;
	}

	auto result = nvme::io::commands::Write::decode_completion(*done);

	std::println("[INFO]: Wrote to the NVMe Namespace 1, 4 LBAs wide");
}

// read from namespace id 1 from starting LBA 0 and read 4 LBAs
{
	dma::DmaBuffer read_buf = dma_heap.alloc(512ul * 4);  // read 1 LBA size
	const nvme::io::commands::Read read_cmd{dma::DmaView{read_buf}, 1, 0x0, 3};

	auto done = controller.execute_io_command(*io_qid, read_cmd);
	if (!done) {
		std::println("[ERROR]: {}", done.error().reason);
		return EXIT_FAILURE;
	}

	if (!done->ok()) {
		std::println("[ERROR]: Command Error");
		return EXIT_FAILURE;
	}

	auto result = nvme::io::commands::Read::decode_completion(*done);

	std::array<std::byte, 2048ull> res{};
	std::memcpy(res.data(), read_buf.uva(), res.size());

	// hexdump the result
	utils::hexdump(res);
}
```

Running this, I got:

```
[INFO]: NVMe Version 1.4
[INFO] Controller is disabled.
AMS: Round Robin is supported
MPSMax: 4
MPSMin: 0
Max Queue Entry Size (CAP.MQES): 2047
NVM Command Set is supported. Setting CC.CSS to NCSS (0b000)
[INFO] Controller is enabled.
NVMe Properties:
        Version ID: 6966
        Serial Number: deadbeef
        Model Number: QEMU NVMe Ctrl                          10.2.2
        Maximum Data Transfer Size: 7
        Controller ID: 0
        Submission Queue Entry Size: 102
        Completion Queue Entry Size: 68
        Number of Namespaces: 256
Active namespaces: 1
Namespace 1:
        Size (NSZE): 2097152 LBAs
        Capacity (NCAP): 2097152 LBAs
        In use (NUSE): 2097152 LBAs
        LBA size: 512 bytes
        Metadata size: 0 bytes
SMART: temperature 323 K, spare 0%, used 0%, read 7 units, written 0 units
Error log entries: 0
# of IOSQs allocated: 64
# of IOCQs allocated: 64
Created IO queue pair (qid 1, cqid 1)
[INFO]: Wrote to the NVMe Namespace 1, 4 LBAs wide
00000000 68 65 6C 6C 6F 20 6E 76 6D 65 00 00 00 00 00 00  |hello nvme......|
00000010 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
00000020 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
00000030 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
00000040 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
00000050 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
00000060 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
```

Yay! I can read what I write.

Current limitation is that all these IO transfers are inside a single page, therefore I am not using PRP lists or SGLs as of now.

I think I will go on with implementing other IO commands, and then return back to that later.