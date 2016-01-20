/*
Copyright (c) 2012, Broadcom Europe Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <linux/ioctl.h>

#define MAJOR_NUM 100
#define IOCTL_MBOX_PROPERTY _IOWR(MAJOR_NUM, 0, char *)
#define DEVICE_FILE_NAME "/dev/vcio"

//static const uint32_t BUS_OFFSET = 0; // This might be right for RPi 1
static const uint32_t BUS_OFFSET = 0xC0000000;

// RPi 2, 'bus' addresses are in C-offset SRAM mapping,
// bypassing L2 cache.
static inline uint32_t BUS_TO_PHYS(uint32_t x) {
  assert(x >= BUS_OFFSET);
  x -= BUS_OFFSET;
  return x;
}

static inline uint32_t PHYS_TO_BUS(uint32_t x) {
  x += BUS_OFFSET;
  assert(x >= BUS_OFFSET);
  return x;
}

int mbox_open();
void mbox_close(int file_desc);

unsigned get_version(int file_desc);
unsigned mem_alloc(int file_desc, unsigned size, unsigned align, unsigned flags);
unsigned mem_free(int file_desc, unsigned handle);
unsigned mem_lock(int file_desc, unsigned handle);
unsigned mem_unlock(int file_desc, unsigned handle);
void *mapmem(unsigned base, unsigned size);
void unmapmem(void *addr, unsigned size);

unsigned execute_code(int file_desc, unsigned code, unsigned r0, unsigned r1, unsigned r2, unsigned r3, unsigned r4, unsigned r5);
unsigned execute_qpu(int file_desc, unsigned num_qpus, unsigned control, unsigned noflush, unsigned timeout);
unsigned qpu_enable(int file_desc, unsigned enable);

struct FrameBufferDesc {
  unsigned char *arm_address;
  unsigned gpu_address;
  unsigned memory_size;
  unsigned pitch;
  unsigned width;
  unsigned height;
  unsigned v_width;
  unsigned v_height;
  unsigned bpp;

  unsigned old_width;
  unsigned old_height;
  unsigned old_v_width;
  unsigned old_v_height;
  unsigned old_bpp;

  unsigned char old_palette[256];
};

unsigned create_frame_buffer(int file_desc, FrameBufferDesc *fbd);
unsigned release_frame_buffer(int file_desc, FrameBufferDesc *fbd);
unsigned set_frame_buffer_pos(int file_desc, unsigned *x, unsigned *y);
unsigned set_frame_buffer_palette(int file_desc, unsigned palette[256]);

uint32_t get_firmware_revision(int fd);
uint32_t get_board_model(int fd);
uint32_t get_board_revision(int fd);
uint64_t get_board_serial(int fd);

