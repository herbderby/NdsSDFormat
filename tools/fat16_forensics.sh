#!/bin/bash
# =============================================================================
# fat16_forensics.sh
# =============================================================================
#
# Forensic analysis of macOS FAT16 formatting choices.
#
# Creates disk images of various sizes, formats them with newfs_msdos -F 16,
# and extracts the key BPB/MBR parameters to understand what values macOS
# chooses for FAT16 volumes.
#
# Usage: ./fat16_forensics.sh [size_in_mb]
#        Default size: 512 MB
#
# Output: Detailed dump of MBR, VBR (BPB), FAT reserved entries, and root
#         directory region.
#
# =============================================================================

set -e

SIZE_MB=${1:-512}
IMAGE_FILE="/tmp/fat16_forensic_${SIZE_MB}MB.img"
LABEL="FORENSIC"

echo "============================================================"
echo "FAT16 Forensic Analysis - ${SIZE_MB} MB image"
echo "============================================================"
echo ""

# -----------------------------------------------------------------------------
# Step 1: Create the image
# -----------------------------------------------------------------------------
echo "[1/6] Creating ${SIZE_MB} MB image at ${IMAGE_FILE}..."
dd if=/dev/zero of="${IMAGE_FILE}" bs=1m count="${SIZE_MB}" 2>/dev/null
echo "      Done."
echo ""

# -----------------------------------------------------------------------------
# Step 2: Attach without mounting
# -----------------------------------------------------------------------------
echo "[2/6] Attaching image..."
ATTACH_OUTPUT=$(hdiutil attach -nomount "${IMAGE_FILE}")
DISK_DEV=$(echo "${ATTACH_OUTPUT}" | awk '{print $1}' | head -1)
echo "      Attached as ${DISK_DEV}"
echo ""

# -----------------------------------------------------------------------------
# Step 3: Format as FAT16
# -----------------------------------------------------------------------------
echo "[3/6] Formatting as FAT16 with label '${LABEL}'..."
if ! newfs_msdos -F 16 -v "${LABEL}" "${DISK_DEV}" 2>&1; then
    echo "      ERROR: newfs_msdos failed. Volume may be too large for FAT16."
    hdiutil detach "${DISK_DEV}" 2>/dev/null || true
    rm -f "${IMAGE_FILE}"
    exit 1
fi
echo "      Done."
echo ""

# -----------------------------------------------------------------------------
# Step 4: Extract raw sectors
# -----------------------------------------------------------------------------
echo "[4/6] Extracting raw sectors..."

# MBR (sector 0)
MBR=$(dd if="${DISK_DEV}" bs=512 count=1 2>/dev/null | xxd -p | tr -d '\n')

# Parse partition table entry 1 (offset 0x1BE = 446)
# Each entry is 16 bytes
PE_STATUS=$(echo "${MBR}" | cut -c$((446*2+1))-$((446*2+2)))
PE_CHS_START=$(echo "${MBR}" | cut -c$((447*2+1))-$((449*2+2)))
PE_TYPE=$(echo "${MBR}" | cut -c$((450*2+1))-$((450*2+2)))
PE_CHS_END=$(echo "${MBR}" | cut -c$((451*2+1))-$((453*2+2)))
# LBA start is 4 bytes little-endian at offset 454
PE_LBA_START_HEX=$(echo "${MBR}" | cut -c$((454*2+1))-$((457*2+2)))
PE_LBA_START=$(printf "%d" 0x$(echo "${PE_LBA_START_HEX}" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/'))
# Sector count is 4 bytes little-endian at offset 458
PE_SECTOR_COUNT_HEX=$(echo "${MBR}" | cut -c$((458*2+1))-$((461*2+2)))
PE_SECTOR_COUNT=$(printf "%d" 0x$(echo "${PE_SECTOR_COUNT_HEX}" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/'))

# MBR signature (bytes 510-511)
MBR_SIG=$(echo "${MBR}" | cut -c$((510*2+1))-$((511*2+2)))

echo "      MBR extracted."

# VBR (first sector of partition)
VBR=$(dd if="${DISK_DEV}" bs=512 skip="${PE_LBA_START}" count=1 2>/dev/null | xxd -p | tr -d '\n')

# Helper to read little-endian values from hex string
read_le16() {
    local hex=$1
    local offset=$2
    local bytes=$(echo "${hex}" | cut -c$((offset*2+1))-$((offset*2+4)))
    printf "%d" 0x$(echo "${bytes}" | sed 's/\(..\)\(..\)/\2\1/')
}

read_le32() {
    local hex=$1
    local offset=$2
    local bytes=$(echo "${hex}" | cut -c$((offset*2+1))-$((offset*2+8)))
    printf "%d" 0x$(echo "${bytes}" | sed 's/\(..\)\(..\)\(..\)\(..\)/\4\3\2\1/')
}

read_byte() {
    local hex=$1
    local offset=$2
    printf "%d" 0x$(echo "${hex}" | cut -c$((offset*2+1))-$((offset*2+2)))
}

read_string() {
    local hex=$1
    local offset=$2
    local len=$3
    echo "${hex}" | cut -c$((offset*2+1))-$(((offset+len)*2)) | xxd -r -p
}

# Parse BPB fields
BPB_BYTES_PER_SEC=$(read_le16 "${VBR}" 11)
BPB_SEC_PER_CLUS=$(read_byte "${VBR}" 13)
BPB_RSVD_SEC_CNT=$(read_le16 "${VBR}" 14)
BPB_NUM_FATS=$(read_byte "${VBR}" 16)
BPB_ROOT_ENT_CNT=$(read_le16 "${VBR}" 17)
BPB_TOT_SEC_16=$(read_le16 "${VBR}" 19)
BPB_MEDIA=$(read_byte "${VBR}" 21)
BPB_FAT_SZ_16=$(read_le16 "${VBR}" 22)
BPB_SEC_PER_TRK=$(read_le16 "${VBR}" 24)
BPB_NUM_HEADS=$(read_le16 "${VBR}" 26)
BPB_HIDD_SEC=$(read_le32 "${VBR}" 28)
BPB_TOT_SEC_32=$(read_le32 "${VBR}" 32)

# FAT16 extended BPB (offset 36+)
BS_DRV_NUM=$(read_byte "${VBR}" 36)
BS_BOOT_SIG=$(read_byte "${VBR}" 38)
BS_VOL_ID=$(read_le32 "${VBR}" 39)
BS_VOL_LAB=$(read_string "${VBR}" 43 11)
BS_FIL_SYS_TYPE=$(read_string "${VBR}" 54 8)

# VBR signature
VBR_SIG=$(echo "${VBR}" | cut -c$((510*2+1))-$((511*2+2)))

echo "      VBR extracted."

# FAT region (first 2 sectors to see reserved entries)
FAT_START_SECTOR=$((PE_LBA_START + BPB_RSVD_SEC_CNT))
FAT_SECTOR_0=$(dd if="${DISK_DEV}" bs=512 skip="${FAT_START_SECTOR}" count=1 2>/dev/null | xxd -p | tr -d '\n')

# FAT[0] and FAT[1] are 16-bit entries at offset 0 and 2
FAT_0=$(read_le16 "${FAT_SECTOR_0}" 0)
FAT_1=$(read_le16 "${FAT_SECTOR_0}" 2)

echo "      FAT reserved entries extracted."

# Root directory region
ROOT_DIR_SECTORS=$(( (BPB_ROOT_ENT_CNT * 32 + BPB_BYTES_PER_SEC - 1) / BPB_BYTES_PER_SEC ))
ROOT_DIR_START=$((FAT_START_SECTOR + BPB_NUM_FATS * BPB_FAT_SZ_16))
ROOT_DIR_SECTOR_0=$(dd if="${DISK_DEV}" bs=512 skip="${ROOT_DIR_START}" count=1 2>/dev/null | xxd -p | tr -d '\n')

# First directory entry (volume label)
DIR_NAME=$(read_string "${ROOT_DIR_SECTOR_0}" 0 11)
DIR_ATTR=$(read_byte "${ROOT_DIR_SECTOR_0}" 11)

echo "      Root directory extracted."
echo ""

# -----------------------------------------------------------------------------
# Step 5: Detach
# -----------------------------------------------------------------------------
echo "[5/6] Detaching image..."
hdiutil detach "${DISK_DEV}" >/dev/null 2>&1
echo "      Done."
echo ""

# -----------------------------------------------------------------------------
# Step 6: Print report
# -----------------------------------------------------------------------------
echo "[6/6] Analysis Report"
echo ""
echo "============================================================"
echo "IMAGE PARAMETERS"
echo "============================================================"
echo "  Image size:              ${SIZE_MB} MB"
echo "  Total sectors:           $((SIZE_MB * 1024 * 1024 / 512))"
echo ""

echo "============================================================"
echo "MASTER BOOT RECORD (Sector 0)"
echo "============================================================"
echo "  MBR Signature:           0x${MBR_SIG}"
echo ""
echo "  Partition Entry 1:"
echo "    PE_status:             0x${PE_STATUS}"
echo "    PE_chsStart:           ${PE_CHS_START}"
echo "    PE_type:               0x${PE_TYPE}"
case "${PE_TYPE}" in
    01) echo "                           (FAT12, CHS)" ;;
    04) echo "                           (FAT16 < 32MB, CHS)" ;;
    06) echo "                           (FAT16 >= 32MB, CHS)" ;;
    0b) echo "                           (FAT32, CHS)" ;;
    0c) echo "                           (FAT32, LBA)" ;;
    0e) echo "                           (FAT16, LBA)" ;;
    *)  echo "                           (Unknown)" ;;
esac
echo "    PE_chsEnd:             ${PE_CHS_END}"
echo "    PE_lbaStart:           ${PE_LBA_START} sectors"
echo "                           ($((PE_LBA_START * 512)) bytes = $((PE_LBA_START * 512 / 1024)) KB)"
echo "    PE_sectorCount:        ${PE_SECTOR_COUNT} sectors"
echo ""

echo "============================================================"
echo "VOLUME BOOT RECORD (Partition Sector 0, Absolute LBA ${PE_LBA_START})"
echo "============================================================"
echo "  VBR Signature:           0x${VBR_SIG}"
echo ""
echo "  BIOS Parameter Block:"
echo "    BPB_BytsPerSec:        ${BPB_BYTES_PER_SEC}"
echo "    BPB_SecPerClus:        ${BPB_SEC_PER_CLUS} (cluster = $((BPB_SEC_PER_CLUS * BPB_BYTES_PER_SEC)) bytes)"
echo "    BPB_RsvdSecCnt:        ${BPB_RSVD_SEC_CNT}"
echo "    BPB_NumFATs:           ${BPB_NUM_FATS}"
echo "    BPB_RootEntCnt:        ${BPB_ROOT_ENT_CNT} (root dir = $((BPB_ROOT_ENT_CNT * 32)) bytes)"
echo "    BPB_TotSec16:          ${BPB_TOT_SEC_16}"
echo "    BPB_Media:             0x$(printf '%02X' ${BPB_MEDIA})"
echo "    BPB_FATSz16:           ${BPB_FAT_SZ_16} sectors"
echo "    BPB_SecPerTrk:         ${BPB_SEC_PER_TRK}"
echo "    BPB_NumHeads:          ${BPB_NUM_HEADS}"
echo "    BPB_HiddSec:           ${BPB_HIDD_SEC}"
echo "    BPB_TotSec32:          ${BPB_TOT_SEC_32}"
echo ""
echo "  FAT16 Extended BPB:"
echo "    BS_DrvNum:             0x$(printf '%02X' ${BS_DRV_NUM})"
echo "    BS_BootSig:            0x$(printf '%02X' ${BS_BOOT_SIG})"
echo "    BS_VolID:              0x$(printf '%08X' ${BS_VOL_ID})"
echo "    BS_VolLab:             \"${BS_VOL_LAB}\""
echo "    BS_FilSysType:         \"${BS_FIL_SYS_TYPE}\""
echo ""

echo "============================================================"
echo "FAT REGION (Starts at absolute LBA ${FAT_START_SECTOR})"
echo "============================================================"
echo "  FAT[0] (media entry):    0x$(printf '%04X' ${FAT_0})"
echo "  FAT[1] (EOC/flags):      0x$(printf '%04X' ${FAT_1})"
echo ""

echo "============================================================"
echo "ROOT DIRECTORY REGION"
echo "============================================================"
echo "  Location:                Absolute LBA ${ROOT_DIR_START}"
echo "  Size:                    ${ROOT_DIR_SECTORS} sectors ($((ROOT_DIR_SECTORS * 512)) bytes)"
echo "  First entry:"
echo "    DIR_Name:              \"${DIR_NAME}\""
echo "    DIR_Attr:              0x$(printf '%02X' ${DIR_ATTR})"
if [ "${DIR_ATTR}" -eq 8 ]; then
    echo "                           (ATTR_VOLUME_ID)"
fi
echo ""

echo "============================================================"
echo "DERIVED LAYOUT"
echo "============================================================"
# Calculate data region start
DATA_START=$((ROOT_DIR_START + ROOT_DIR_SECTORS))
# Calculate cluster count
if [ "${BPB_TOT_SEC_16}" -ne 0 ]; then
    TOTAL_SEC=${BPB_TOT_SEC_16}
else
    TOTAL_SEC=${BPB_TOT_SEC_32}
fi
DATA_SECTORS=$((TOTAL_SEC - BPB_RSVD_SEC_CNT - BPB_NUM_FATS * BPB_FAT_SZ_16 - ROOT_DIR_SECTORS))
CLUSTER_COUNT=$((DATA_SECTORS / BPB_SEC_PER_CLUS))

echo "  Data region start:       Absolute LBA ${DATA_START}"
echo "  Data sectors:            ${DATA_SECTORS}"
echo "  Cluster count:           ${CLUSTER_COUNT}"
if [ "${CLUSTER_COUNT}" -lt 4085 ]; then
    echo "  FAT type (by clusters):  FAT12"
elif [ "${CLUSTER_COUNT}" -lt 65525 ]; then
    echo "  FAT type (by clusters):  FAT16"
else
    echo "  FAT type (by clusters):  FAT32"
fi
echo ""

# -----------------------------------------------------------------------------
# Cleanup
# -----------------------------------------------------------------------------
rm -f "${IMAGE_FILE}"
echo "Image deleted. Analysis complete."
