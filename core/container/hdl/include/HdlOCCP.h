
/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2010
 *
 *    Mercury Federal Systems, Incorporated
 *    1901 South Bell Street
 *    Suite 402
 *    Arlington, Virginia 22202
 *    United States of America
 *    Telephone 703-413-0781
 *    FAX 703-413-0784
 *
 *  This file is part of OpenCPI (www.opencpi.org).
 *     ____                   __________   ____
 *    / __ \____  ___  ____  / ____/ __ \ /  _/ ____  _________ _
 *   / / / / __ \/ _ \/ __ \/ /   / /_/ / / /  / __ \/ ___/ __ `/
 *  / /_/ / /_/ /  __/ / / / /___/ ____/_/ / _/ /_/ / /  / /_/ /
 *  \____/ .___/\___/_/ /_/\____/_/    /___/(_)____/_/   \__, /
 *      /_/                                             /____/
 *
 *  OpenCPI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenCPI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OCCP_H
#define OCCP_H
#ifndef __KERNEL__ // for linux driver
#include <stdint.h>
#endif

#ifdef __cplusplus
namespace OCPI {
  namespace HDL {
#endif
#define OCCP_MAX_WORKERS 15
#define OCCP_MAX_REGIONS 16
    typedef struct {
      uint32_t birthday; // The time the final build was started
      uint8_t uuid[16];  // The official UUID of the bitstream
      char platform[16]; // The name of the platform the bitstream was built for
      char device[16];   // The name of the device the bitstream was built for
      char load[4];      // Information to load this particular device on this platform
      char dna[8];       // The serial number of this particular device
    } HdlUUID;
    typedef struct {
      const uint64_t magic; // 00
      const uint32_t
        revision,           // 08
        birthday,           // 0c
        config,             // 10 - bit mask of existing workers
        pciDevice,          // 14
        attention,          // 18
        status;             // 1c
      uint32_t
        scratch20,          // 20
        scratch24,          // 24
        control,            // 28
        reset;              // 2c
      const uint32_t
	timeStatus;         // 30
      uint32_t
        timeControl;        // 34
      uint64_t
        time,               // 383c
        timeDelta;          // 4044
      const uint32_t
        timeClksPerPps,     // 48
	readCounter;        // 4c
      const uint64_t dna;   // 5054
      const uint32_t
        pad1[9],            // 585c6064686c707478
        numRegions,         // 7c
        regions[OCCP_MAX_REGIONS];// 8084888c9094989ca0a4a8acb0b4b8bc
      HdlUUID uuid;         // c0...
    } OccpAdminRegisters;
    typedef struct {
      const uint32_t
        initialize,
        start,
        stop,
        release,
        test,
        beforeQuery,
        afterConfigure,
        reserved7,
        status;
      uint32_t
        control;
      const uint32_t
        lastConfig;
      uint32_t
        clearError,
	window,
        reserved[3];
    } OccpWorkerRegisters;
#define OCCP_WORKER_CONTROL_ENABLE 0x80000000
#define OCCP_WORKER_CONTROL_SIZE 0x10000
#define OCCP_ADMIN_SIZE OCCP_WORKER_CONTROL_SIZE
#define OCCP_ADMIN_CONFIG_OFFSET 4096
#define OCCP_ADMIN_CONFIG_SIZE 4096
#define OCCP_WORKER_CONFIG_WINDOW_BITS 20
#define OCCP_WORKER_CONFIG_SIZE (1 << OCCP_WORKER_CONFIG_WINDOW_BITS)
#define OCCP_WORKER_CONTROL_BASE(i) ((i) * OCCP_BYTES_PER_WORKER)
#define OCCP_WORKER_PROPERTY_BASE(i) ((i) * OCCP_BYTES_PER_WORKER)
// Magic values from config space: these values are 32 bit values with chars big endian
#define OCCP_MAGIC1 (('O'<<24)|('p'<<16)|('e'<<8)|'n')
#define OCCP_MAGIC2 (('C'<<24)|('P'<<16)|('I'<<8))
#define OCCP_MAGIC ((((uint64_t)OCCP_MAGIC2) << 32) | OCCP_MAGIC1)
// Read return values from workers
#define OCCP_ERROR_RESULT   0xc0de4202
#define OCCP_TIMEOUT_RESULT 0xc0de4203
#define OCCP_RESET_RESULT   0xc0de4204
#define OCCP_SUCCESS_RESULT 0xc0de4201
#define OCCP_FATAL_RESULT   0xc0de4205
#define OCCP_STATUS_CONFIG_WRITE (1 << 27)
#define OCCP_STATUS_CONFIG_OP (0x7 << 24)
#define OCCP_STATUS_CONFIG_BE (0xf << 20)

#define OCCP_STATUS_CONFIG_ADDR_VALID (1 << 16)
#define OCCP_STATUS_CONFIG_BE_VALID (1 << 17)
#define OCCP_STATUS_CONFIG_OP_VALID (1 << 18)
#define OCCP_STATUS_CONFIG_WRITE_VALID (1 << 19)
#define OCCP_STATUS_CONFIG_ADDR_VALID (1 << 16)
#define OCCP_STATUS_ACCESS_ERROR      (1 << 11) // this is simulated on the CPU, not in OCCP
#define OCCP_STATUS_FINISHED        (1 << 10)
#define OCCP_STATUS_ATTENTION       (1 << 9)
#define OCCP_STATUS_WRITE_TIMEOUT   (1 << 8)
#define OCCP_STATUS_READ_TIMEOUT    (1 << 7)
#define OCCP_STATUS_CONTROL_TIMEOUT (1 << 6)
#define OCCP_STATUS_WRITE_FAIL      (1 << 5)
#define OCCP_STATUS_READ_FAIL       (1 << 4)
#define OCCP_STATUS_CONTROL_FAIL    (1 << 3)
#define OCCP_STATUS_WRITE_ERROR     (1 << 2)
#define OCCP_STATUS_READ_ERROR      (1 << 1)
#define OCCP_STATUS_CONTROL_ERROR   (1 << 0)
#define OCCP_STATUS_WRITE_ERRORS	 \
    (OCCP_STATUS_WRITE_TIMEOUT |	 \
     OCCP_STATUS_WRITE_FAIL |		 \
     OCCP_STATUS_WRITE_ERROR)
#define OCCP_STATUS_READ_ERRORS		 \
    (OCCP_STATUS_READ_TIMEOUT |		 \
     OCCP_STATUS_READ_FAIL |		 \
     OCCP_STATUS_READ_ERROR)
#define OCCP_STATUS_CONTROL_ERRORS	 \
    (OCCP_STATUS_CONTROL_TIMEOUT |	 \
     OCCP_STATUS_CONTROL_FAIL |		 \
     OCCP_STATUS_CONTROL_ERROR)
#define OCCP_STATUS_ALL_ERRORS 0x9ff
    typedef uint8_t *OccpConfigArea;
    typedef struct {
      OccpWorkerRegisters control;
      uint8_t pad[OCCP_WORKER_CONTROL_SIZE - sizeof(OccpWorkerRegisters)];
    } OccpWorker;

    typedef struct {
      OccpAdminRegisters admin;
      uint8_t pad[OCCP_ADMIN_CONFIG_OFFSET - sizeof(OccpAdminRegisters)];
      uint8_t configRam[OCCP_ADMIN_CONFIG_SIZE];
      uint8_t pad1[OCCP_ADMIN_SIZE - (OCCP_ADMIN_CONFIG_OFFSET + OCCP_ADMIN_CONFIG_SIZE)];
      OccpWorker worker[OCCP_MAX_WORKERS];
      uint8_t config[OCCP_MAX_WORKERS][OCCP_WORKER_CONFIG_SIZE];
    } OccpSpace;
#ifdef __cplusplus
    inline uint64_t swap32(uint64_t x) {return (x <<32) | (x >> 32); }
  }
}
#endif
#endif
