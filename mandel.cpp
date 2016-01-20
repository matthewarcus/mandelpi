#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/ioctl.h>
#include <algorithm>
#include <curses.h> // For key definitions
#include <termios.h>

#include "mailbox.h"

// cached=0xC; direct=0x4
static const uint32_t GPU_MEM_FLG = 0x04;
static const uint32_t MEM_MAP = 0x0;
//static const uint32_t GPU_MEM_FLG = 0x0C; // pi v1?
//static const uint32_t MEM_MAP = 0x20000000; // pi v1?

//#define IO_BASE     0x20000000 // pi v1
#define IO_BASE     0x3F000000 // pi v2
#define IO_LEN      0x01000000

bool verbose = false;

static volatile bool terminated = false;

void sigint_handler(int) {
  terminated = true;
}

void setsighandler(bool runonce = true) {
  struct sigaction sigact;
  memset(&sigact,0,sizeof(sigact));
  sigact.sa_handler = sigint_handler;
  if (runonce) sigact.sa_flags |= SA_RESETHAND;
  if (sigaction(SIGINT, &sigact, NULL) != 0) {
    fprintf(stderr, "sigaction failed: %s\n",
	    strerror(errno));
  }
}

int width = 1280;
int height = 720;

float xcentre = -0.7449;
float ycentre = 0.1;
int maxiterations = 256;

float xscale = 1;
float xinc = 0;
float xzoom = 1;

static const int MAXQPUS = 16;
static const int MAXUNIFS = 16;
static const int MAXBLOCKS = 4;

static const uint32_t hexcode[] = {
  #include HEXFILE
};

#define ARRAYSIZE(a) ((sizeof(a))/sizeof((a)[0]))

#define DEBUG(...) (fprintf(stderr,__VA_ARGS__))
//#define DEBUG(...)
#define ERROR(...) (fprintf(stderr,"Error: " __VA_ARGS__))

#define REG(x) ((0xC00000 + x) >>2)
//#define REG(x) ((x)>>2)

#define	BLOCK_SIZE (4*1024)

// Address base is at 0x7ec00000 on BCM2835
// Translates to IO_BASE + c00000 + register offset

// V3D spec: http://www.broadcom.com/docs/support/videocore/VideoCoreIV-AG100-R.pdf
#define V3D_IDENT0   REG(0x00)
#define V3D_IDENT1   REG(0x04)
#define V3D_IDENT2   REG(0x08)
#define V3D_SQCNTL   REG(0x418)
#define V3D_VPACNTL  REG(0x500)
#define V3D_VPMBASE  REG(0x504)
#define V3D_PCTRC    REG(0x670)
#define V3D_PCTRE    REG(0x674)
#define V3D_PCTR(i) (REG(0x680) + 2*i)
#define V3D_PCTRS(i)(REG(0x684) + 2*i)
#define V3D_ERRSTAT  REG(0xf20)

#define V3D_SQRSV0   REG(0x410)
#define V3D_SQRSV1   REG(0x414)

#define V3D_L2CACTL (0xC00020>>2)
#define V3D_SLCACTL (0xC00024>>2)
#define V3D_SRQPC   (0xC00430>>2)
#define V3D_SRQUA   (0xC00434>>2)
#define V3D_SRQUL   (0xC00438>>2)
#define V3D_SRQCS   (0xC0043c>>2)

#define V3D_DBCFG   (0xC00e00>>2)
#define V3D_DBQITE  (0xC00e2c>>2)
#define V3D_DBQITC  (0xC00e30>>2)

struct GPUData;

struct GPU {
  GPUData *data;
  uint32_t datasize;
  uint32_t handle;
  uint32_t vc;
};

// GPUland pointers to uniforms and code
struct GPUControl {
  uint32_t punifs;
  uint32_t pcode;
};

volatile uint32_t *peri = NULL;

// Last is first like in the doc.
static inline int BITS(uint32_t n, int last, int first) {
  return (n >> first) & ((1<<(last+1-first))-1);
}

#define PRINTREG(REG) (fprintf(stderr,"%-12s %08x\n", #REG ":", peri[REG]))

int gpu_prepare(GPU &gpu, size_t datasize)
{
  peri = (volatile uint32_t*) mapmem(IO_BASE,IO_LEN);
  if (!peri) {
    perror("Can't allocate peri");
    exit(0);
  }
  GPUData* ptr;
  int mb = mbox_open();
  if (mb < 0) {
    ERROR("mbox_open() failed: %d\n", mb);
    return -1;
  }
  if (qpu_enable(mb, 1) != 0) {
    ERROR("qpu_enable() failed");
    return -2;
  }
  if (peri) {
    fprintf(stderr,"IO_BASE: %08x\n", IO_BASE);
    fprintf(stderr,"IO_LEN:  %08x\n", IO_LEN);
    uint32_t ident0 = peri[V3D_IDENT0];
    uint32_t ident1 = peri[V3D_IDENT1];
    uint32_t ident2 = peri[V3D_IDENT2];
    // Can't do char width reads on a register so copy and cast.
    fprintf(stderr, "%.3s %08x %08x %08x\n",
	    (char*)&ident0, ident0, ident1, ident2);
    fprintf(stderr, "VPMSZ=%dK HDRT=%d NSEM=%d TUPS=%d QUPS=%d NSLC=%d, REV=%d\n",
	    BITS(ident1,31,28), BITS(ident1,27,24), BITS(ident1,23,16), 
	    BITS(ident1,15,12), BITS(ident1,11,8), BITS(ident1,7,4),
	    BITS(ident1,3,0));
    fprintf(stderr, "TLBDB=%d TLBSZ=%d VRISZ=%d\n",
	    BITS(ident2,11,8), BITS(ident2,7,4), BITS(ident2,3,0));
    PRINTREG(V3D_ERRSTAT);
    PRINTREG(V3D_DBQITE);
    PRINTREG(V3D_L2CACTL);
    PRINTREG(V3D_SRQCS); // Queue control
  }

  peri[V3D_SRQCS] = 0;
  //peri[V3D_ERRSTAT] = 0; // Any way to clear this?

#if 0
  if (peri) {
    // This extends the VPM memory available to GPU programs
    // but there doesn't seem to be any way of using it.
    // (see errata to V3D spec)
    peri[V3D_VPMBASE] = (1<<6)-1;
    fprintf(stderr,"%08x\n",peri[V3D_VPMBASE]);
    fprintf(stderr,"%08x\n",(1<<6)-1);
  }
#endif

  uint32_t handle = mem_alloc(mb, datasize, 4096, GPU_MEM_FLG);
  DEBUG("handle=%x\n", handle);
  if (handle == 0) {
    ERROR("mem_alloc() failed\n");
    qpu_enable(mb, 0);
    return -3;
  }
  uint32_t vc = mem_lock(mb, handle); // GPU address
  DEBUG("vc=%x\n", vc);
  ptr = (GPUData*)mapmem(BUS_TO_PHYS(vc + MEM_MAP), datasize);
  DEBUG("ptr=%p\n", ptr);
  if (ptr == NULL) {
    ERROR("mapmem() failed\n");
    // goto error?
    mem_free(mb, handle);
    mem_unlock(mb, handle);
    qpu_enable(mb, 0);
    return -4;
  }

  gpu.vc = vc;
  gpu.handle = handle;
  gpu.data = (GPUData*)ptr;
  gpu.datasize = datasize;
  return mb;
}

uint32_t gpu_execute(int mb, uint32_t control, int nqpus)
{
  //DEBUG("msg=%x\n", gpu.vc);
  return execute_qpu(mb,
                     nqpus,
                     control, //gpu.vc + offsetof(struct GPUData, control),
                     1 /* no flush */,
                     5000 /* timeout */);
}

void gpu_release(int mb, GPU &gpu)
{
  unmapmem((void*)gpu.data, gpu.datasize);
  mem_unlock(mb, gpu.handle);
  mem_free(mb, gpu.handle);
  qpu_enable(mb, 0);
  mbox_close(mb);
}

// Performance counters

#define DEFCOUNTER(name,n) { #name, n }

struct Counter {
  const char* name;
  int index;
  uint32_t value;
} counters[] = {
  DEFCOUNTER(QPU_TOTAL_IDLE, 13),
  DEFCOUNTER(QPU_TOTAL_VERTEX, 14),
  DEFCOUNTER(QPU_TOTAL_FRAGMENT, 15),
  DEFCOUNTER(QPU_TOTAL_VALID, 16),
  DEFCOUNTER(QPU_TOTAL_TMU_STALL, 17),
  DEFCOUNTER(QPU_TOTAL_SCOREBOARD_STALL, 18),
  //DEFCOUNTER(QPU_TOTAL_VARYINGS_STALL, 19),
  DEFCOUNTER(QPU_TOTAL_ICACHE_HITS, 20),
  DEFCOUNTER(QPU_TOTAL_ICACHE_MISSES, 21),
  DEFCOUNTER(QPU_TOTAL_UCACHE_HITS, 22),
  DEFCOUNTER(QPU_TOTAL_UCACHE_MISSES, 23),
  DEFCOUNTER(QPU_TOTAL_TMU_PROCESSED, 24),
  DEFCOUNTER(QPU_TOTAL_TMU_MISSES, 25),
  DEFCOUNTER(QPU_TOTAL_VDW_STALL, 26),
  DEFCOUNTER(QPU_TOTAL_VCD_STALL, 27),
  DEFCOUNTER(QPU_TOTAL_L2_HITS, 28),
  DEFCOUNTER(QPU_TOTAL_L2_MISSES, 29)
};

#undef DEFCOUNTER

void counter_setup() {
  int ncounters = ARRAYSIZE(counters);
  assert(ncounters <= 16);
  for (int i = 0; i < ncounters; i++) {
    uint32_t reg = V3D_PCTRS(i);
    //fprintf(stderr,"Setting counter %08x to %d\n", 4*reg, counters[i]);
    peri[reg] = counters[i].index;
  }
  // Counters only seem to work if top bit in PCTRE is set
  peri[V3D_PCTRE] = 0x8000ffff;
}

void counter_clear() {
  peri[V3D_PCTRC] = 0x0000ffff;
}

void counter_read() {
  int ncounters = ARRAYSIZE(counters);
  for (int i = 0; i < ncounters; i++) {
    counters[i].value = peri[V3D_PCTR(i)];
  }
}

void counter_print() {
  int ncounters = ARRAYSIZE(counters);
  for (int i = 0; i < ncounters; i++) {
    fprintf(stderr, "%s: %u\n",
	    counters[i].name, counters[i].value);
  }
}

#define GPU_TIMEOUT 5000

unsigned gpu_execute_direct(GPUControl *control, int num_qpus) {
    time_t limit = 0;

    peri[V3D_DBCFG] = 0;   // Disallow IRQ
    peri[V3D_DBQITE] = 0;  // Disable IRQ
    peri[V3D_DBQITC] = -1; // Resets IRQ flags

    peri[V3D_L2CACTL] =  1<<2; // Clear L2 cache
    peri[V3D_SLCACTL] = -1;    // Clear other caches

    peri[V3D_SRQCS] = (1<<7) | (1<<8) | (1<<16); // Reset error bit and counts
    peri[V3D_SRQCS] = (1<<0); // Clear queue

    //PRINTREG(V3D_SRQCS); // Queue control
    if (0) {
      uint32_t srqcs = peri[V3D_SRQCS];
      fprintf(stderr,"QPURQCC=%d QPURQCM=%d QPURQERR=%d QPURQL=%d\n",
	      BITS(srqcs,23,16),BITS(srqcs,15,8),
	      BITS(srqcs,7,7),BITS(srqcs,5,0));
    }
    for (int q = 0; q < num_qpus; q++) { // Launch shader(s)
        peri[V3D_SRQUA] = control[q].punifs;
        peri[V3D_SRQPC] = control[q].pcode;
    }
    if (0) {
      uint32_t srqcs = peri[V3D_SRQCS];
      fprintf(stderr,"QPURQCC=%d QPURQCM=%d QPURQERR=%d QPURQL=%d\n",
	      BITS(srqcs,23,16),BITS(srqcs,15,8),
	      BITS(srqcs,7,7),BITS(srqcs,5,0));
    }
    //PRINTREG(V3D_SRQCS); // Queue control

    // Busy wait polling
    for (;;) {
      // Could do something useful here, like output some counts
      // or even nanosleep a bit.
      int q = 1000;
      do {
	if (((peri[V3D_SRQCS]>>16) & 0xff) == (uint32_t)num_qpus) { // All done?
	  return 0;
	}
      } while (--q);
      if (!limit) {
	limit = clock() + CLOCKS_PER_SEC / 1000 * GPU_TIMEOUT;
      } else if (clock() >= limit) {
	// TODO: some cleanup required?
	return -1;
      }
    }
}

// Program data
#define NROWS 16
#define ROWLEN 16
#define VPMSIZE (4*16*16)

// This is what is actually passed to the GPU
struct GPUData {
  uint32_t input[NROWS*ROWLEN];
  GPUControl control[MAXQPUS];
  uint32_t unifs[MAXQPUS][MAXUNIFS];
  uint32_t output[VPMSIZE];
  uint32_t code[ARRAYSIZE(hexcode)];
};

uint32_t floattoint(float x) {
  return *(uint32_t*)&x;
}

float scale = 0.02;
float xorigin = -1;
float yorigin = -0.5;

float PI = 3.14159;
unsigned int palette[256];
int fbfd = -1;
int kbfd = -1;
struct termios saved_attributes;

FrameBufferDesc fbd;

void setscale(GPUData *gpudata, int nqpus) {
  fprintf(stderr, "setscale: %10.10g %10.10g %10.10g\n", xcentre, ycentre, xscale);
  int swidth = fbd.width;
  int sheight = fbd.height;
  scale = 1/(xscale * sheight/2);
  xorigin = xcentre-scale*swidth/2;
  yorigin = ycentre-scale*sheight/2;
  for (int i = 0; i < nqpus; i++) {
    gpudata->unifs[i][9]  = maxiterations; // maximum iterations
    gpudata->unifs[i][10] = floattoint(xorigin); // x0
    gpudata->unifs[i][11] = floattoint(yorigin); // y0
    gpudata->unifs[i][12] = floattoint(scale); // scale
  }
}

void getframebuffer(int mb, FrameBufferDesc &fbd, int width, int height) {
  memset(&fbd, 0, sizeof(fbd));  
  fbd.width = width;
  fbd.height = height;
  fbd.v_width = width;
  fbd.v_height = height*2;
  fbd.bpp = 8;
  if (!create_frame_buffer(mb, &fbd)) {
    DEBUG("Frame buffer failure\n");
    exit(1);
  }
  fprintf(stderr,"memory size: %d\n", fbd.memory_size);
  fprintf(stderr,"width: %d\n", fbd.width);
  fprintf(stderr,"height: %d\n", fbd.height);
  fprintf(stderr,"bpp: %d\n", fbd.bpp);
  fprintf(stderr,"vwidth: %d\n", fbd.v_width);
  fprintf(stderr,"vheight: %d\n", fbd.v_height);
  fprintf(stderr,"pitch: %d\n", fbd.pitch);
  fprintf(stderr,"gpu address: %x\n", fbd.gpu_address);
  fprintf(stderr,"physical address: %x\n", BUS_TO_PHYS(fbd.gpu_address));
  fprintf(stderr,"bus increment: %x\n", fbd.gpu_address & 0xC0000000);
  uint32_t actual_memory_size = fbd.memory_size;
  uint32_t expected_memory_size = fbd.pitch * fbd.v_height * (fbd.bpp/8);;
  fprintf(stderr,"memory size: %x\n", actual_memory_size);
  fprintf(stderr,"expected: %x\n", expected_memory_size); 

  // For some reason, we sometimes get the wrong memory size in 16-bit mode,
  if (actual_memory_size < expected_memory_size) {
    fprintf(stderr, "Weird memory size or pitch, aborting\n");
    fprintf(stderr, "Retry after fbset -depth 32 or fbset -depth 8 \n");
    exit(0);
  }
  memset(fbd.arm_address, 0x55, fbd.memory_size);
}

uint32_t setfb(FrameBufferDesc &fbd, int mb, int n){
  uint32_t offset;
  uint32_t xfb, yfb;
  if (n == 0) {
    xfb = 0; yfb = fbd.height;
    offset = 0;
  } else {
    xfb = 0; yfb = 0;
    offset = fbd.pitch * fbd.height;
  }
  set_frame_buffer_pos(mb, &xfb, &yfb);
  return offset;
}

void setpalette(int mb)
{
  palette[0] = 0;
  for (int i = 1; i < 256; ++i) {
    float f = 2*PI * i / maxiterations;

    int k = 255; int j = 150;
    int r = j + cos(f + PI/3) * k;
    int g = j + cos(f + 3 * PI / 3) * k;
    int b = j + cos(f + 5 * PI / 3) * k;

    r = std::min(r, 255);
    g = std::min(g, 255);
    b = std::min(b, 255);
    r = r < 0 ? 0 : r;
    g = g < 0 ? 0 : g;
    b = b < 0 ? 0 : b;
    palette[i] =  (b << 16) | (g << 8) | r;
  }
  if (!set_frame_buffer_palette(mb, palette)) {
    fprintf(stderr, "error: can't set palette\n");
  }
}

void rotatepalette(int mb)
{
  unsigned int tmp = palette[1];
  for (int i = 1; i < 255; ++i) {
    palette[i] = palette[i+1];
  }
  palette[255] = tmp;
  if (!set_frame_buffer_palette(mb, palette)) {
    fprintf(stderr, "error: can't set palette\n");
  }
}

void
reset_input_mode (void)
{
  fprintf(stderr, "Restoring input mode\n");
  tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}

void
set_input_mode (void)
{
  struct termios tattr;

  /* Make sure stdin is a terminal. */
  if (!isatty (STDIN_FILENO))
    {
      fprintf (stderr, "Not a terminal.\n");
      exit (EXIT_FAILURE);
    }

  /* Save the terminal attributes so we can restore them later. */
  tcgetattr (STDIN_FILENO, &saved_attributes);
  atexit (reset_input_mode);

  /* Set the funny terminal modes. */
  tcgetattr (STDIN_FILENO, &tattr);
  tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
  tattr.c_cc[VMIN] = 0; // Change this to 0 for non-blocking input.
  tattr.c_cc[VTIME] = 0;
  tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}

void appsetup(GPUData *gpudata, int nqpus, int mb) {
  const char *kbfds = "/dev/tty0";
  set_input_mode();
  //needed for vsync...
  fbfd = open("/dev/fb0", O_RDWR);
  if (fbfd < 0) {
    fprintf(stderr, "Error: cannot open framebuffer device.\n");
  }
  getframebuffer(mb, fbd, width, height);
  kbfd = open(kbfds, O_WRONLY);
  if (kbfd >= 0) {
    ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
  }
  // Set up "control" at start
  uint32_t offset = setfb(fbd, mb, 1);
  for (int i = 0; i < nqpus; i++) {
    gpudata->unifs[i][4] = fbd.gpu_address + offset;
    gpudata->unifs[i][5] = fbd.width;   // Width
    gpudata->unifs[i][6] = fbd.height;  // Height
    gpudata->unifs[i][7] = fbd.pitch;   // Pitch
    gpudata->unifs[i][8] = fbd.bpp;     // Depth
  }
  setscale(gpudata, nqpus);
  setpalette(mb);
}

int termchar() {
  int state = 0;
  while (true) {
    int c = getchar();
    if (c < 0) return c;
    //fprintf(stderr, "%02x\n", c);
    if (state == 0) {
      switch (c) {
      case 0x1b: state = 1; break;
      default: state = 0;
      }
    } else if (state == 1) {
      switch (c) {
      case 0x5b: state = 2; break;
      default: state = 0;
      }
    } else if (state == 2) {
      switch (c) {
      case 0x41: return KEY_UP;
      case 0x42: return KEY_DOWN;
      case 0x43: return KEY_LEFT;
      case 0x44: return KEY_RIGHT;
      case 0x35: return KEY_PPAGE;
      case 0x36: return KEY_NPAGE;
      default: state = 0;
      }
    }
    if (state == 0) return c;
  }
}

void appprepare(GPUData *gpudata, int nqpus, int mb, unsigned i) {
  (void)gpudata; (void)nqpus; (void)mb, (void)i;
  bool handled = false;
  while (!handled && !terminated) {
    handled = true;
    int ch = -1;
    while (true) {
      int tmp = termchar();
      if (tmp == 0x7e) continue;
      if (tmp >= 0) ch = tmp;
      else break;
    }
    float inc = 1/(5*xscale);
    float zoom = 1.1;
    switch (ch) {
    case 's': case KEY_UP:
      ycentre -= inc;
      break;
    case 'd': case KEY_DOWN:
      ycentre += inc;
      break;
    case 'a': case KEY_LEFT:
      xcentre += inc;
      break;
    case 'f': case KEY_RIGHT:
      xcentre -= inc;
      break;
    case 'w': case KEY_PPAGE:
      xscale *= zoom;
      break;
    case 'x': case KEY_NPAGE:
      xscale /= zoom;
      break;
    case ' ':
      rotatepalette(mb);
      handled = false;
      break;
    case 'n':
      if (maxiterations >= 16) maxiterations /= 2;
      fprintf(stderr, "Maxiterations now %d\n", maxiterations);
      break;
    case 'm':
      maxiterations *= 2;
      fprintf(stderr, "Maxiterations now %d\n", maxiterations);
      break;
    default:
      handled = false;
    }
  }
  xscale *= xzoom;
  xcentre += xinc;
  setscale(gpudata, nqpus);
}  

void appupdate(GPUData *gpudata, int nqpus, int mb, unsigned i) {
  uint32_t offset = setfb(fbd, mb, i%2);
  // Do vsync after flipping the buffers (to avoid writing
  // before the flip actually happens?).
  if (ioctl(fbfd, FBIO_WAITFORVSYNC, 0) != 0) {
    fprintf(stderr, "FBIO_WAITFORVSYNC failed: %s\n",
	    strerror(errno));
  }
  for (int i = 0; i < nqpus; i++) {
    gpudata->unifs[i][4] = fbd.gpu_address + offset;
  }
}

void append(GPUData *gpudata, int nqpus, int mb) {
  while(false) {
    // Get errors if called too frequently
    rotatepalette(mb);
    timespec t;
    memset(&t, 0, sizeof(t));
    t.tv_nsec = 1000*1000*50;
    nanosleep(&t, NULL);
  }
  if (verbose) {
    // Print a slice of the FB for debugging purposes.
    for (int i = 0; i < 4*64; i++) {
      for (int j = 0; j < 64; j++) {
	fprintf(stdout, "%02x", (fbd.arm_address)[i*fbd.pitch+j]);
      }
      fprintf(stdout, "\n");
    }
  }
  if (kbfd >= 0) {
    ioctl(kbfd, KDSETMODE, KD_TEXT);
    close(kbfd);
  }
  release_frame_buffer(mb, &fbd);
  close(fbfd);
}

void setup(GPUData *gpudata, uint32_t gpubase, int nqpus, int mb) {
  fprintf(stderr, "gpubase: %08x\n", gpubase);
  memset((void*)gpudata,0,sizeof(gpudata));
  // Set up "control" at start
  for (int i = 0; i < nqpus; i++) {
    gpudata->control[i].punifs =
      gpubase +
      offsetof(struct GPUData, unifs) +
      i*sizeof(gpudata->unifs[0]);
    gpudata->control[i].pcode = gpubase + offsetof(struct GPUData, code);;
    gpudata->unifs[i][0] = gpubase + offsetof(struct GPUData, input);
    gpudata->unifs[i][1] = gpubase + offsetof(struct GPUData, output);
    gpudata->unifs[i][2] = i;
    gpudata->unifs[i][3] = nqpus;
  }
  memcpy((void*)gpudata->code, hexcode, sizeof gpudata->code);
}

// A general purpose driver function
int main(int argc, char *argv[]) {
  int nqpus = 12;
  int exec_direct = false;
  argc--; argv++;
  if (argc > 0) {
    nqpus = std::min(MAXQPUS,(int)strtoul(argv[0],NULL,0));
    argc--; argv++;
  }

  struct GPU gpu;
  size_t datasize = sizeof(struct GPUData);
  int mb = gpu_prepare(gpu, datasize);
  if (mb < 0) return mb; // Should have already reported error

  uint32_t firmware = get_firmware_revision(mb);
  uint32_t model = get_board_model(mb);
  uint32_t revision = get_board_revision(mb);
  uint64_t serial = get_board_serial(mb);
  fprintf(stderr, "firmware: %x\n", firmware);
  fprintf(stderr, "model: %d\n", model);
  fprintf(stderr, "revision: %x\n", revision);
  fprintf(stderr, "serial: %llx\n", serial);
  setup(gpu.data, gpu.vc, nqpus, mb);
  appsetup(gpu.data, nqpus, mb);

  setsighandler();

  // We could use the GPU timer registers for this
  timespec start, end;
  counter_setup();
  int exec;
  for (unsigned i = 0; !terminated; i++) {
    if (i > 0) appprepare(gpu.data, nqpus, mb, i);

    clock_gettime(CLOCK_MONOTONIC,&start);
    counter_clear();
    exec = exec_direct ? gpu_execute_direct(gpu.data->control, nqpus)
      : gpu_execute(mb, gpu.vc + offsetof(GPUData,control), nqpus);
    counter_read();
    clock_gettime(CLOCK_MONOTONIC,&end);

    if (exec != 0) {
      fprintf(stderr, "gpu_execute failed: %d\n", exec);
    }
    // I doubt if the clock granularity is down to ns
    int tdiff = (end.tv_sec - start.tv_sec) * (1000 * 1000) + (end.tv_nsec - start.tv_nsec)/1000;
    fprintf(stderr,"Time =  %d usecs\n", tdiff);
    appupdate(gpu.data, nqpus, mb, i);
    //fprintf(stderr,"%d\n", i);
  }
  counter_print();
  PRINTREG(V3D_ERRSTAT);
  if (peri[V3D_ERRSTAT] & ~(1<<12)) {
    fprintf(stderr,"There were errors!\n");
  }
  
  //PRINTREG(V3D_DBQITC); // Not very interesting usually
  PRINTREG(V3D_SRQCS);    // Queue control
  append(gpu.data, nqpus, mb);
  gpu_release(mb, gpu);
}
