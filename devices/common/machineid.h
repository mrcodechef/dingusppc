/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-21 divingkatae and maximum
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

#ifndef MACHINE_ID_H
#define MACHINE_ID_H

#include <devices/common/hwcomponent.h>
#include <devices/common/mmiodevice.h>

#include <cinttypes>

/**
    @file Contains definitions for PowerMacintosh machine ID registers.

    The machine ID register is a memory-based register containing hardcoded
    values the system software can read to identify machine/board it's running on.

    Register location and value meaning are board-dependent.
 */

/**
    Machine ID register for Nubus Power Macs.
    It's located at physical address 0x5FFFFFFC and contains four bytes:
      +0 uint16_t signature = 0xA55A
      +1 uint8_t  machine_type (3 - PowerMac)
      +2 uint8_t  model (0x10 = PDM, 0x12 = Carl Sagan, 0x13 = Cold Fusion)
 */
class NubusMacID : public MMIODevice {
public:
    NubusMacID(const uint16_t id) {
        this->name = "Nubus-Machine-id";
        this->id[0] = 0xA5;
        this->id[1] = 0x5A;
        this->id[2] = (id >> 8) & 0xFF;
        this->id[3] = id & 0xFF;
        supports_types(HWCompType::MMIO_DEV);
    };
    ~NubusMacID() = default;

    uint32_t read(uint32_t reg_start, uint32_t offset, int size) {
        return (offset < 4 ? this->id[offset] : 0);
    };

    /* not writable */
    void write(uint32_t reg_start, uint32_t offset, uint32_t value, int size) {};

private:
    uint8_t id[4];
};

/**
    The machine ID for the Gossamer board is accesible at 0xFF000004 (phys).
    It contains a 16-bit value revealing machine's capabilities like bus speed,
    ROM speed, I/O configuration etc.
    Because the meaning of these bits is poorly documented, the code below
    simply return a raw value obtained from real hardware.
 */
class GossamerID : public MMIODevice {
public:
    GossamerID(const uint16_t id) {
        this->id = id, this->name = "Machine-id";
        supports_types(HWCompType::MMIO_DEV);
    };
    ~GossamerID() = default;

    uint32_t read(uint32_t reg_start, uint32_t offset, int size) {
        return ((!offset && size == 2) ? this->id : 0);
    };

    /* not writable */
    void write(uint32_t reg_start, uint32_t offset, uint32_t value, int size) {};

private:
    uint16_t id;
};

#endif /* MACHINE_ID_H */
