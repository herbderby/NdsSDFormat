# Master Boot Record (MBR) for x86 Architecture

A comprehensive technical reference for the Master Boot Record structure and bootstrap process on x86 systems.

---

## Table of Contents

1. [Overview](#overview)
2. [MBR Bootstrap Process](#mbr-bootstrap-process)
3. [MBR Structure](#mbr-structure)
4. [Partition Table Entry Format](#partition-table-entry-format)
5. [CHS Addressing](#chs-addressing)
6. [Traditional MBR Implementation](#traditional-mbr-implementation)
7. [Dual Booting](#dual-booting)
8. [Generic MBRs](#generic-mbrs)
9. [Building a Custom MBR Bootstrap](#building-a-custom-mbr-bootstrap)
   - [Initial Environment](#initial-environment)
   - [Immediate Priorities](#immediate-priorities)
10. [Writing an MBR to Disk](#writing-an-mbr-to-disk)
11. [Implementation Example](#implementation-example)
12. [References](#references)

---

## Overview

The **Master Boot Record (MBR)** is the bootsector of a hard disk—the very first sector (sector 0) that the BIOS loads and executes when booting from that disk. The MBR contains two critical components:

1. **MBR Bootstrap Program** - Executable code (up to 446 bytes)
2. **Partition Table** - Up to four partition entries describing the disk layout

All devices that emulate a hard disk during system initialization must contain an MBR with a valid partition table, even if they are not bootable.

### Boot Sequence

The BIOS will only boot an MBR from a device if:
- The device is in the boot sequence stored in CMOS
- The MBR is formatted correctly (ends with the boot signature `0x55AA`)

Even if a device is not in the boot sequence, a Real Mode program (such as another MBR or bootloader) can still load and boot that device's MBR directly using the device's drive number.

---

## MBR Bootstrap Process

When the BIOS loads an MBR, the following occurs:

1. The MBR is loaded to **physical address 0x7C00**
2. The **DL register** is set to the drive number from which the MBR was loaded
3. The BIOS jumps to **0x7C00** (beginning of the MBR bootstrap code)

### Typical MBR Bootstrap Behavior

A standard MBR bootstrap program performs the following steps:

1. **Relocate itself** away from 0x7C00 (using memory copy and usually a far jump)
   - Common relocation targets: 0x0600 or 0x7A00

2. **Determine which partition to boot**
   - Look for the active partition (boot flag set to 0x80)
   - Or present user with selection menu

3. **Update partition table if necessary**
   - If user selected an inactive partition, mark it active
   - Clear active bits from other partitions
   - Rewrite MBR using BIOS INT 13h if modified

4. **Load the Volume Boot Record (VBR)**
   - Use BIOS INT 13h to load the bootsector from the selected partition
   - Load to physical address 0x7C00

5. **Prepare for handoff**
   - Set DS:SI pointing to the selected partition table entry
   - Ensure DL contains the drive number

6. **Transfer control**
   - Jump to 0x7C00 with CS=0 and DL=drive number

**Important**: The DL register value and DS:SI pointer should be passed all the way to the kernel. The relocated MBR must not be overwritten during boot, as DS:SI points to data within it.

---

## MBR Structure

The MBR occupies exactly **512 bytes** (one sector) with the following layout:

| Offset | Size (bytes) | Description |
|--------|--------------|-------------|
| 0x000  | 440¹         | **MBR Bootstrap code** (flat binary executable) |
| 0x1B8  | 4            | Optional Unique Disk ID / Signature² |
| 0x1BC  | 2            | Optional, reserved (usually 0x0000)³ |
| 0x1BE  | 16           | **First partition table entry** |
| 0x1CE  | 16           | **Second partition table entry** |
| 0x1DE  | 16           | **Third partition table entry** |
| 0x1EE  | 16           | **Fourth partition table entry** |
| 0x1FE  | 2            | **Boot signature** (0x55, 0xAA) |

### Notes

¹ **Bootstrap Size**: Can be extended to 446 bytes if the optional Disk ID and reserved fields are omitted.

² **Unique Disk ID**: A 4-byte identifier used by modern Linux and Windows systems to uniquely identify drives attached to a system. "Unique" means distinct within a particular system's set of attached drives.

³ **Reserved Field**: Usually 0x0000. A value of 0x5A5A indicates the disk should be treated as read-only (according to some sources).

### Important Constraints

- **Partition table entries are NOT aligned** on 32-bit boundaries
- Naming entries as "First" through "Fourth" is for convenience only—they need not be in any particular order
- **Any one** of the partitions may be marked "active"
- There should be **at most one** active partition
- Windows requires that the partition it boots from be marked active
- Most other operating systems ignore the active bit

---

## Partition Table Entry Format

Each partition table entry is **16 bytes** with the following structure:

| Offset¹ | Size (bytes) | Description |
|---------|--------------|-------------|
| 0x00    | 1            | **Boot indicator** (0x80 = active/bootable, 0x00 = inactive) |
| 0x01    | 3            | **CHS address of partition start** |
| 0x04    | 1            | **Partition type** (system ID) |
| 0x05    | 3            | **CHS address of last partition sector** |
| 0x08    | 4            | **LBA of partition start** (little-endian) |
| 0x0C    | 4            | **Number of sectors in partition** (little-endian) |

¹ *Offset is relative to the start of the partition table entry*

### Boot Indicator (Offset 0x00)

- **0x80** - Active/bootable partition (bit 7 set)
- **0x00** - Inactive partition
- Only one partition should be marked active (0x80)

### Partition Type (Offset 0x04)

Common partition type values:

| Value  | Description |
|--------|-------------|
| 0x00   | Empty/unused entry |
| 0x01   | FAT12 |
| 0x04   | FAT16 (partitions < 32 MB) |
| 0x05   | Extended partition (CHS) |
| 0x06   | FAT16 (partitions ≥ 32 MB) |
| 0x07   | NTFS/exFAT/HPFS |
| 0x0B   | FAT32 (CHS) |
| 0x0C   | FAT32 (LBA) |
| 0x0E   | FAT16 (LBA) |
| 0x0F   | Extended partition (LBA) |
| 0x82   | Linux swap |
| 0x83   | Linux filesystem |
| 0xEE   | GPT protective MBR |

### LBA vs. CHS Addressing

- **LBA (Logical Block Addressing)**: Modern 32-bit sector addressing (offsets 0x08 and 0x0C)
- **CHS (Cylinder-Head-Sector)**: Legacy addressing format (offsets 0x01 and 0x05)

Modern systems use LBA exclusively. CHS is limited to 8 GB and is largely obsolete.

---

## CHS Addressing

CHS (Cylinder-Head-Sector) addressing is a legacy format for specifying disk locations. Each CHS address is encoded in **3 bytes**:

### CHS Encoding Format

**For Starting CHS (offset 0x01) and Ending CHS (offset 0x05):**

| Byte | Bits | Field | Range |
|------|------|-------|-------|
| 0    | 7-0  | **Head** | 0-255 |
| 1    | 7-6  | **Cylinder** (high 2 bits) | 0-1023 (10 bits total) |
| 1    | 5-0  | **Sector** | 1-63 |
| 2    | 7-0  | **Cylinder** (low 8 bits) | 0-1023 (10 bits total) |

### Extracting CHS Values

```c
uint8_t chs[3];  // 3-byte CHS address from partition table

uint8_t  head     = chs[0];
uint16_t cylinder = ((chs[1] & 0xC0) << 2) | chs[2];
uint8_t  sector   = chs[1] & 0x3F;
```

### Converting CHS to LBA

```c
LBA = (cylinder * heads_per_cylinder + head) * sectors_per_track + (sector - 1)
```

**Note**: Sector numbering in CHS starts at 1, not 0.

### CHS Limitations

- Maximum addressable: **1024 cylinders × 256 heads × 63 sectors ≈ 8 GB**
- Modern disks use LBA exclusively
- CHS fields may contain invalid values (0xFFFE or 0xFFFFFF) for partitions beyond CHS limits

---

## Traditional MBR Implementation

The **DOS FDISK** program created the first standard MBR, which became the de facto standard for MBR functionality. This MBR was never changed after its initial introduction.

### FDISK MBR Behavior

The FDISK MBR implementation:

1. **Relocates itself to 0x0000:0x0600**
2. **Examines partition table** at offsets 0x1BE, 0x1CE, 0x1DE, and 0x1EE
3. **Identifies active partition** (boot indicator = 0x80)
4. **Loads first sector** of active partition (DOS bootsector) to 0x0000:0x7C00
5. **Sets SI register** to point to the active partition table entry
6. **Jumps to 0x7C00** - transferring control to the DOS bootsector

### FDISK Operations

- **Partitioning a blank disk**: Writes MBR to sector 0
- **Adding a partition**: Adds entry to the partition table in the MBR
- **Making a partition active**: Sets the boot indicator byte to 0x80

This minimal implementation provides only basic functionality: booting a single active partition from the first disk.

---

## Dual Booting

A typical system may have:
- Multiple hard disks
- 4 standard partitions per disk (excluding extended partitions)
- Each partition potentially containing a distinct bootable OS

### Standard Boot Limitations

The standard x86 boot sequence:
- Only boots the MBR from the first disk ("C:") found during detection
- Standard MBR allows only one active partition on that disk
- Can only boot that single partition

### Custom Dual Boot MBR

To enable dual booting, replace the standard MBR with a custom MBR that:

1. **Allows selection** of any partition on the current drive
2. **Optionally allows** selection of other hard disks
3. **Supports partition selection** across multiple disks

### Chain Loading

**Chain loading** occurs when one MBR loads and executes another MBR from a different drive. If all MBRs support dual booting, users can cycle through all disks and choose the correct partition.

### Size Limitations

The MBR bootstrap is limited to ~440-446 bytes of code, which is insufficient for a sophisticated user interface. Solutions include:

1. **Basic interface** - Minimal text-based selection (fits in MBR)
2. **Bootloader delegation** - MBR loads a more capable bootloader from a partition
   - Bootloader can provide sophisticated UI
   - Much larger code space available

### Automatic Boot

If only one partition exists on one drive, a well-designed MBR should:
- Boot that partition automatically
- Skip user prompts entirely

---

## Generic MBRs

Every disk partitioning application must write some form of MBR to the disk. Fortunately, they all conform to the FDISK standard:

### Standard Conformance

All generic MBRs will:
- Load your bootloader at **0x7C00**
- Set **DL register** to the boot drive number
- Set **DS:SI** pointing to the correct partition table entry

### Common MBR Sources

- DOS FDISK
- Linux fdisk/gdisk
- Windows Disk Management
- Third-party partitioning tools (Partition Manager, GParted, etc.)
- Bootloader installers (GRUB, LILO, etc.)

While some may add dual-boot or other features, they maintain compatibility with the standard interface.

---

## Building a Custom MBR Bootstrap

### Requirements and Constraints

An MBR bootstrap program must:
- Run entirely in **Real Mode** (16-bit mode)
- Be written in **Assembly** (self-relocation is required)
- Be exactly **512 bytes** total size
- Have bootstrap code of **at most 446 bytes** (440 bytes if using Disk ID)
- End with boot signature **0x55AA** at offset 0x1FE
- Contain **at least one partition table entry** (some UEFI firmware requires this)

### Why Assembly?

- Self-relocation is one of the things C cannot do
- Most modern C compilers cannot generate Real Mode-compatible code
- Assembly provides complete control over the exact binary output

---

## Initial Environment

### Memory and Register State at MBR Load

When the BIOS loads and executes an MBR:

| Register/Memory | Value | Notes |
|----------------|-------|-------|
| **Physical address** | 0x7C00 | MBR loaded here |
| **CS:IP** | Usually 0x0000:0x7C00 | Sometimes 0x07C0:0x0000 (same physical address) |
| **DL** | Drive number | Only reliable value from BIOS (must be saved!) |
| **All other registers** | Undefined | Cannot be relied upon |
| **Most of memory** | Undefined | Must initialize everything |

### CS:IP Ambiguity

Some BIOSes use:
- `CS = 0x0000, IP = 0x7C00` (most common)
- `CS = 0x07C0, IP = 0x0000` (less common)

Both resolve to physical address 0x7C00, but cause different behavior.

**Best Practice**: Use a far jump early in bootstrap to enforce consistent CS:IP values.

```nasm
; Enforce CS = 0, IP = known_value
jmp 0x0000:start
start:
    ; Now we know CS = 0
```

---

## Immediate Priorities

### 1. Set Up Stack

**Critical**: Immediately establish a valid stack.

```nasm
cli                 ; Disable interrupts during setup
xor ax, ax
mov ss, ax          ; SS = 0
mov sp, 0x7C00      ; SP = 0x7C00 (stack grows down from 0x7C00)
sti                 ; Re-enable interrupts
```

**Stack Location Options**:
- **0x7C00** - If MBR relocates elsewhere
- **Just below relocated MBR** - If MBR relocates to 0x0600 or 0x7A00
- **0x7000-0x7C00** - Safe region below MBR

### 2. Initialize Segment Registers

Set all segment registers to known values:

```nasm
xor ax, ax          ; AX = 0
mov ds, ax          ; DS = 0
mov es, ax          ; ES = 0
mov fs, ax          ; FS = 0 (if used)
mov gs, ax          ; GS = 0 (if used)
```

### 3. Save Drive Number

**Critical**: Save the DL register value immediately:

```nasm
mov [bootDrive], dl    ; Save to variable
```

This is the **only** information reliably provided by BIOS.

### Memory Layout Considerations

Available low memory during boot: **0x500 to 0x7FFFF** (see x86 Memory Map)

**Common MBR Relocation Addresses**:

| Address | Reason |
|---------|--------|
| 0x0600  | Traditional (used by FDISK MBR) |
| 0x7A00  | Just below 0x7C00 (minimizes fragmentation) |
| 0x0500  | Start of usable low memory |

**Relocation Example**:

```nasm
; Copy MBR from 0x7C00 to 0x0600
mov cx, 256         ; 512 bytes = 256 words
mov si, 0x7C00      ; Source
mov di, 0x0600      ; Destination
rep movsw           ; Copy

; Far jump to relocated code
jmp 0x0000:relocated_start

relocated_start:
    ; Now executing from 0x0600
```

---

## Writing an MBR to Disk

### Tools Required

To write an MBR to a disk, you need special disk I/O tools because:
- The MBR exists on the "raw device" outside any partition
- Standard filesystem tools cannot access sector 0 directly
- Low-level disk access is required

### Common Disk Tools

**Linux**:
```bash
# Write MBR to disk (DANGEROUS - verify device name!)
sudo dd if=mbr.bin of=/dev/sda bs=512 count=1

# Backup existing MBR first
sudo dd if=/dev/sda of=mbr_backup.bin bs=512 count=1
```

**Windows**:
- RawWrite / Win32DiskImager
- Disk editing utilities (HxD, WinHex)

**Cross-platform**:
- QEMU (for testing images)
- VirtualBox (mount raw disk image)
- Disk image utilities (see OSDev wiki)

### Testing Safely

**Always test in a virtual machine first**:

```bash
# Create blank disk image
dd if=/dev/zero of=disk.img bs=1M count=100

# Write MBR to image
dd if=mbr.bin of=disk.img bs=512 count=1 conv=notrunc

# Test in QEMU
qemu-system-i386 -drive file=disk.img,format=raw
```

---

## Implementation Example

Below is a simple but complete MBR implementation that loads and boots the first active partition:

```nasm
[bits 16]
[org 0x0600]

start:
    cli                         ; Disable interrupts
    xor ax, ax                  ; Zero AX
    mov ds, ax                  ; Set Data Segment to 0
    mov es, ax                  ; Set Extra Segment to 0
    mov ss, ax                  ; Set Stack Segment to 0
    mov sp, 0x7C00              ; Set Stack Pointer below MBR
    
.CopyLower:
    mov cx, 0x0100              ; 256 WORDs in MBR (512 bytes)
    mov si, 0x7C00              ; Current MBR Address
    mov di, 0x0600              ; New MBR Address
    rep movsw                   ; Copy MBR
    
    jmp 0:LowStart              ; Jump to new address (set CS=0)

LowStart:
    sti                         ; Re-enable interrupts
    mov BYTE [bootDrive], dl    ; Save boot drive number
    
.CheckPartitions:               ; Find bootable partition
    mov bx, PT1                 ; Base = First Partition Table Entry
    mov cx, 4                   ; 4 partition entries to check
    
.CKPTloop:
    mov al, BYTE [bx]           ; Get boot indicator
    test al, 0x80               ; Check for active bit
    jnz .CKPTFound              ; Found active partition
    add bx, 0x10                ; Move to next entry (16 bytes)
    dec cx                      ; Decrement counter
    jnz .CKPTloop               ; Continue loop
    jmp ERROR                   ; No active partition found!
    
.CKPTFound:
    mov WORD [PToff], bx        ; Save partition entry offset
    add bx, 8                   ; Move to LBA address field
    
.ReadVBR:
    mov ebx, DWORD [bx]         ; Get LBA start of active partition
    mov di, 0x7C00              ; Load VBR to 0x7C00
    mov cx, 1                   ; Read one sector
    call ReadSectors            ; Read VBR
    
.jumpToVBR:
    cmp WORD [0x7DFE], 0xAA55   ; Check for boot signature
    jne ERROR                   ; Invalid bootsector
    mov si, WORD [PToff]        ; DS:SI = partition table entry
    mov dl, BYTE [bootDrive]    ; DL = drive number
    jmp 0x7C00                  ; Jump to VBR

ERROR:
    ; Display error message and halt
    mov si, ErrorMsg
    call PrintString
    cli
    hlt

; ReadSectors function (to be implemented)
; Input: EBX = LBA, CX = sector count, ES:DI = buffer, [bootDrive] = drive
ReadSectors:
    ; Implementation using INT 13h, AH=42h (Extended Read)
    ; Left as exercise - see BIOS INT 13h documentation
    ret

PrintString:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp PrintString
.done:
    ret

ErrorMsg db "No bootable partition found", 0x0D, 0x0A, 0

; Pad to maintain proper structure
times (218 - ($-$$)) nop        ; Pad for disk timestamp

DiskTimeStamp times 8 db 0      ; Optional disk timestamp

bootDrive db 0                  ; Boot drive variable
PToff dw 0                      ; Partition table entry offset

times (0x1B4 - ($-$$)) nop      ; Pad to Disk ID

UID times 10 db 0               ; Unique Disk ID + reserved

; Partition Table
PT1 times 16 db 0               ; First partition entry
PT2 times 16 db 0               ; Second partition entry
PT3 times 16 db 0               ; Third partition entry
PT4 times 16 db 0               ; Fourth partition entry

dw 0xAA55                       ; Boot signature
```

### Notes on Example

This example demonstrates:
- **Relocation** from 0x7C00 to 0x0600
- **Stack setup** at 0x7C00
- **Partition table scanning** for active partition
- **VBR loading** using LBA address
- **Validation** of boot signature
- **Proper handoff** with DS:SI and DL set

**What's missing**:
- Complete `ReadSectors` implementation (requires INT 13h knowledge)
- Error handling for disk read failures
- Support for CHS addressing fallback
- User interaction for partition selection

---

## References

### Specifications

1. **IBM PC/AT Technical Reference** - Original MBR specification
2. **BIOS Boot Specification** - Phoenix Technologies
3. **UEFI Specification** - MBR compatibility in UEFI systems

### Online Resources

- [OSDev Wiki - MBR (x86)](https://wiki.osdev.org/MBR_(x86))
- [OSDev Wiki - Partition Table](https://wiki.osdev.org/Partition_Table)
- [OSDev Wiki - ATA in x86 RealMode (BIOS)](https://wiki.osdev.org/ATA_in_x86_RealMode_(BIOS))
- [The Boot Process](http://www.nondot.org/sabre/os/articles/TheBootProcess/)

### Tools and Utilities

- **John Fine's SMBMBR** - Example dual-boot MBR code
  - [Download](http://www.osdever.net/downloads/bootsectors/smbmbr03.zip)
- **Ranish Partition Manager** - Disk partitioning with advanced MBR
  - [Website](http://www.ranish.com/part/)

### Further Reading

- Charles Petzold - *Code: The Hidden Language of Computer Hardware and Software*
- Ralf Brown's Interrupt List - INT 13h documentation
- x86 Assembly Language Programming textbooks

---

## Appendix A: Complete Partition Table Entry Example

Example of a FAT32 partition starting at LBA 2048, containing 1,048,576 sectors:

```
Offset  Value       Field               Description
------  ----------  ------------------  ------------------------------------
0x00    0x80        Boot Indicator      Active/bootable partition
0x01    0x00        Start Head          Head 0
0x02    0x21        Start Sector/Cyl    Sector 1, Cylinder 0 (0x0001)
0x03    0x00        Start Cylinder      Cylinder 0 (low byte)
0x04    0x0C        Partition Type      FAT32 (LBA)
0x05    0xFE        End Head            Head 254
0x06    0xFF        End Sector/Cyl      Sector 63, Cylinder 1023 (0x03FF)
0x07    0xFF        End Cylinder        Cylinder 1023 (low byte)
0x08    0x00000800  LBA Start           Sector 2048 (little-endian)
0x0C    0x00100000  Sector Count        1,048,576 sectors (little-endian)
```

## Appendix B: Common Partition Type Codes

| Type | Description |
|------|-------------|
| 0x00 | Empty |
| 0x01 | FAT12 |
| 0x04 | FAT16 < 32MB |
| 0x05 | Extended (CHS) |
| 0x06 | FAT16 |
| 0x07 | NTFS/exFAT |
| 0x0B | FAT32 (CHS) |
| 0x0C | FAT32 (LBA) |
| 0x0E | FAT16 (LBA) |
| 0x0F | Extended (LBA) |
| 0x27 | Hidden NTFS |
| 0x42 | Windows Dynamic |
| 0x82 | Linux swap |
| 0x83 | Linux |
| 0x85 | Linux extended |
| 0x8E | Linux LVM |
| 0xA5 | FreeBSD |
| 0xA6 | OpenBSD |
| 0xA8 | Mac OS X |
| 0xA9 | NetBSD |
| 0xAF | Mac OS X HFS+ |
| 0xBE | Solaris boot |
| 0xEE | GPT protective |
| 0xEF | EFI System |
| 0xFB | VMware VMFS |
| 0xFC | VMware swap |
| 0xFD | Linux RAID |

---

*Document compiled from OSDev Wiki and IBM PC/AT Technical References*
