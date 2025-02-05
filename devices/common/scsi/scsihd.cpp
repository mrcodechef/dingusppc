/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-22 divingkatae and maximum
                      (theweirdo)     spatium

(Contact divingkatae#1017 or powermax#2286 on Discord for more info)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/** @file Generic SCSI Hard Disk emulation. */

#include <devices/common/scsi/scsi.h>
#include <devices/common/scsi/scsihd.h>
#include <devices/deviceregistry.h>
#include <loguru.hpp>
#include <machines/machinebase.h>
#include <machines/machineproperties.h>
#include <memaccess.h>

#include <fstream>
#include <cstring>
#include <stdio.h>
#include <sys/stat.h>

#define HDD_SECTOR_SIZE 512

using namespace std;

ScsiHardDisk::ScsiHardDisk(int my_id) : ScsiDevice(my_id) {
    supports_types(HWCompType::SCSI_DEV);
}

void ScsiHardDisk::insert_image(std::string filename) {
    //We don't want to store everything in memory, but
    //we want to keep the hard disk available.
    this->hdd_img.open(filename, ios::out | ios::in | ios::binary);

    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    if (!rc) {
        this->img_size = stat_buf.st_size;
        this->total_blocks = (this->img_size + HDD_SECTOR_SIZE - 1) / HDD_SECTOR_SIZE;
    } else {
        ABORT_F("ScsiHardDisk: could not determine file size using stat()");
    }
    this->hdd_img.seekg(0, std::ios_base::beg);
}

void ScsiHardDisk::process_command() {
    uint32_t lba          = 0;
    uint16_t transfer_len = 0;
    uint16_t alloc_len    = 0;
    uint8_t  param_len    = 0;

    uint8_t  page_code    = 0;
    uint8_t  subpage_code = 0;

    this->pre_xfer_action  = nullptr;
    this->post_xfer_action = nullptr;

    // assume successful command execution
    this->status = ScsiStatus::GOOD;

    uint8_t* cmd = this->cmd_buf;

    if (cmd[0] != 0 && cmd[0] != 8 && cmd[0] != 0xA && cmd[0] != 0x28
        && cmd[0] != 0x2A && cmd[0] != 0x25) {
        ABORT_F("SCSI-HD: untested command 0x%X", cmd[0]);
    }

    switch (cmd[0]) {
    case ScsiCommand::TEST_UNIT_READY:
        test_unit_ready();
        break;
    case ScsiCommand::REWIND:
        rewind();
        break;
    case ScsiCommand::REQ_SENSE:
        alloc_len = cmd[4];
        req_sense(alloc_len);
        break;
    case ScsiCommand::INQUIRY:
        this->inquiry();
        break;
    case ScsiCommand::READ_6:
        lba = ((cmd[1] & 0x1F) << 16) + (cmd[2] << 8) + cmd[3];
        transfer_len = cmd[4];
        read(lba, transfer_len, 6);
        break;
    case ScsiCommand::READ_10:
        lba          = (cmd[2] << 24) + (cmd[3] << 16) + (cmd[4] << 8) + cmd[5];
        transfer_len = (cmd[7] << 8) + cmd[8];
        read(lba, transfer_len, 10);
        break;
    case ScsiCommand::WRITE_6:
        lba          = ((cmd[1] & 0x1F) << 16) + (cmd[2] << 8) + cmd[3];
        transfer_len = cmd[4];
        write(lba, transfer_len, 6);
        break;
    case ScsiCommand::WRITE_10:
        lba          = (cmd[2] << 24) + (cmd[3] << 16) + (cmd[4] << 8) + cmd[5];
        transfer_len = (cmd[7] << 8) + cmd[8];
        write(lba, transfer_len, 10);
        this->switch_phase(ScsiPhase::DATA_OUT);
        break;
    case ScsiCommand::SEEK_6:
        lba         = ((cmd[1] & 0x1F) << 16) + (cmd[2] << 8) + cmd[3];
        seek(lba);
        break;
    case ScsiCommand::MODE_SELECT_6:
        param_len = cmd[4];
        mode_select_6(param_len);
        break;
    case ScsiCommand::MODE_SENSE_6:
        this->mode_sense_6();
        break;
    case ScsiCommand::READ_CAPACITY_10:
        this->read_capacity_10();
        break;
    default:
        LOG_F(WARNING, "SCSI_HD: unrecognized command: %x", cmd[0]);
    }
}

bool ScsiHardDisk::prepare_data() {
    switch (this->cur_phase) {
    case ScsiPhase::DATA_IN:
        this->data_ptr  = (uint8_t*)this->img_buffer;
        this->data_size = this->cur_buf_cnt;
        break;
    case ScsiPhase::DATA_OUT:
        this->data_ptr  = (uint8_t*)this->img_buffer;
        this->data_size = 0;
        break;
    case ScsiPhase::STATUS:
        if (!error) {
            this->img_buffer[0] = ScsiStatus::GOOD;
        } else {
            this->img_buffer[0] = ScsiStatus::CHECK_CONDITION;
        }
        this->cur_buf_cnt = 1;
        break;
    case ScsiPhase::MESSAGE_IN:
        this->img_buffer[0] = this->msg_code;
        this->cur_buf_cnt = 1;
        break;
    default:
        LOG_F(WARNING, "SCSI_HD: unexpected phase in prepare_data");
        return false;
    }

    return true;
}

int ScsiHardDisk::test_unit_ready() {
    this->switch_phase(ScsiPhase::STATUS);
    return ScsiError::NO_ERROR;
}

int ScsiHardDisk::req_sense(uint16_t alloc_len) {
    if (alloc_len != 252) {
        LOG_F(WARNING, "Inappropriate Allocation Length: %d", alloc_len);
    }
    return ScsiError::NO_ERROR;    // placeholder - no sense
}

void ScsiHardDisk::inquiry() {
    int page_num  = cmd_buf[2];
    int alloc_len = cmd_buf[4];

    if (page_num) {
        ABORT_F("SCSI_CDROM: invalid page number in INQUIRY");
    }

    if (alloc_len >= 36) {
        this->img_buffer[0] = 0;       // device type: Direct-access block device (hard drive)
        this->img_buffer[1] = 0x80;    // removable media
        this->img_buffer[2] = 2;       // ANSI version: SCSI-2
        this->img_buffer[3] = 1;       // response data format
        this->img_buffer[4] = 0x1F;    // additional length
        this->img_buffer[5] = 0;
        this->img_buffer[6] = 0;
        this->img_buffer[7] = 0x18;    // supports synchronous xfers and linked commands
        std::memcpy(img_buffer + 8, vendor_info, 8);
        std::memcpy(img_buffer + 16, prod_info, 16);
        std::memcpy(img_buffer + 32, rev_info, 4);

        this->bytes_out  = 36;
        this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;

        this->switch_phase(ScsiPhase::DATA_IN);
    }
    else {
        LOG_F(WARNING, "Inappropriate Allocation Length: %d", alloc_len);
    }
}

int ScsiHardDisk::send_diagnostic() {
    return 0x0;
}

int ScsiHardDisk::mode_select_6(uint8_t param_len) {
    if (param_len == 0) {
        return 0x0;
    }
    else {
        LOG_F(WARNING, "Mode Select calling for param length of: %d", param_len);
        return param_len;
    }
}

void ScsiHardDisk::mode_sense_6() {
    uint8_t page_code  = this->cmd_buf[2] & 0x3F;
    uint8_t alloc_len = this->cmd_buf[4];
}

void ScsiHardDisk::read_capacity_10() {
    uint32_t lba = READ_DWORD_BE_U(&this->cmd_buf[2]);

    if (this->cmd_buf[1] & 1) {
        ABORT_F("SCSI-HD: RelAdr bit set in READ_CAPACITY_10");
    }

    if (!(this->cmd_buf[8] & 1) && lba) {
        LOG_F(ERROR, "SCSI-HD: non-zero LBA for PMI=0");
        this->status = ScsiStatus::CHECK_CONDITION;
        this->sense  = ScsiSense::ILLEGAL_REQ;
        this->switch_phase(ScsiPhase::STATUS);
        return;
    }

    uint32_t last_lba = this->total_blocks - 1;
    uint32_t blk_len  = HDD_SECTOR_SIZE;

    WRITE_DWORD_BE_A(&img_buffer[0], last_lba);
    WRITE_DWORD_BE_A(&img_buffer[4], blk_len);

    this->cur_buf_cnt = 8;
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;

    this->switch_phase(ScsiPhase::DATA_IN);
}


void ScsiHardDisk::format() {
}

void ScsiHardDisk::read(uint32_t lba, uint16_t transfer_len, uint8_t cmd_len) {
    uint32_t transfer_size = transfer_len;

    std::memset(img_buffer, 0, sizeof(img_buffer));

    if (cmd_len == 6 && transfer_len == 0) {
        transfer_size = 256;
    }

    transfer_size *= HDD_SECTOR_SIZE;
    uint64_t device_offset = lba * HDD_SECTOR_SIZE;

    this->hdd_img.seekg(device_offset, this->hdd_img.beg);
    this->hdd_img.read(img_buffer, transfer_size);

    this->cur_buf_cnt = transfer_size;
    this->msg_buf[0] = ScsiMessage::COMMAND_COMPLETE;

    this->switch_phase(ScsiPhase::DATA_IN);
}

void ScsiHardDisk::write(uint32_t lba, uint16_t transfer_len, uint8_t cmd_len) {
    uint32_t transfer_size = transfer_len;

    if (cmd_len == 6 && transfer_len == 0) {
        transfer_size = 256;
    }

    transfer_size *= HDD_SECTOR_SIZE;
    uint64_t device_offset = lba * HDD_SECTOR_SIZE;

    this->incoming_size = transfer_size;

    this->hdd_img.seekg(device_offset, this->hdd_img.beg);

    this->post_xfer_action = [this]() {
        this->hdd_img.write(this->img_buffer, this->incoming_size);
    };
}

void ScsiHardDisk::seek(uint32_t lba) {
    uint64_t device_offset = lba * HDD_SECTOR_SIZE;
    this->hdd_img.seekg(device_offset, this->hdd_img.beg);
}

void ScsiHardDisk::rewind() {
    this->hdd_img.seekg(0, this->hdd_img.beg);
}

static const PropMap SCSI_HD_Properties = {
    {"hdd_img", new StrProperty("")},
    {"hdd_wr_prot", new BinProperty(0)},
};

static const DeviceDescription SCSI_HD_Descriptor =
    {ScsiHardDisk::create, {}, SCSI_HD_Properties};

REGISTER_DEVICE(ScsiHD, SCSI_HD_Descriptor);
