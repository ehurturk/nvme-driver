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

