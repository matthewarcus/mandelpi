# mandelpi
Mandelbrot set generation with Pi 2 GPU

An exercise in GPU programming for the Raspberry Pi 2.

To run:

$ sudo ./mandel [<num QPUs>]

sudo is necessary to access the GPU & mailbox control registers.

By default, the program uses all 12 QPUs, fewer may be specified on the command line.

The program is designed to be run from a terminal and controlled by the keyboard. I use an ssh terminal; I haven't tried running it directly from the Pi.

Controls are:

* Arrow keys: move left, right, up, down
* Page up: move in
* Page down: move down
* m: increase maximum number of iterations
* n: decrease maximum number of iterations
* space: rotate colour palette, hold down for continuous rotation
* Ctrl-C: terminate program and clean up

To build, just type "make". You will need to have installed the excellent vc4asm by Marcel Müller: https://github.com/maazl/vc4asm. Follow instructions there for installation & change VC4ROOT in the mandelpi Makefile to the appropriate location.

For another Mandelbrot on the PI GPU, see https://github.com/fraka3000/Mandelbrot-qpu - I came across this after I had done the main QPU code, so that's quite different, but it was useful in getting the mailbox framebuffer interface working.

I haven't tried this on a Pi 1 or Zero, some of the RAM addresses will have to be changed (see constants at top of mailbox.h and mandel.cpp).

Screen size is fixed at 1280x720, I haven't tried other sizes.

There seems to be a bug in the RPi firmware where changing the framebuffer virtual size and depth simultaneously fails if the total framebuffer size doesn't change (the wrong line pitch is returned from the mailbox call) so best to set the screen depth to 8 before starting the program ('fbset -depth 8' should do it) - the program attempts to detect anomalous values, but as usual, nothing is guaranteed.
