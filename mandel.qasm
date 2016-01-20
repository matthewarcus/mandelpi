.set fbout,  ra1
.set col,    ra2
.set row,    ra3
.set count,  ra4
.set x0,     ra5
.set y1,     ra6
.set x,      ra7
.set i,      ra8
.set index,  ra9  # QPU index
.set nqpus,  ra10 # Total number of QPUs

# Most uniforms in B registers
.set input,  rb1
.set output, rb2
.set fbbase, rb3
.set width,  rb4
.set height, rb5
.set pitch,  rb6
.set depth,  rb7
.set iters,  rb8
.set xorigin,rb9
.set yorigin,rb10
.set scale,  rb11

.set x1,     rb12
.set y0,     rb13
.set y,      rb14
.set inc,    rb15

# Get our uniforms
mov input, unif   # 0
mov output, unif
mov index, unif
mov nqpus, unif
mov fbbase,  unif
mov width, unif   # 5
mov height, unif
mov pitch, unif
mov depth, unif
mov iters, unif
mov xorigin, unif # 10
mov yorigin, unif
mov scale, unif

# Clear VPM (easier debugging)
mov count, 64
mov vw_setup, vpm_setup(64, 1, h32(0,0))
:vpmclearloop
sub.setf count, count, 1
brr.anynz -, :vpmclearloop
mov vpm, 0x12345678 # Test value
nop # Branch delay
nop # Branch delay

# Fill the framebuffer from the VPM
# mov r0, width
# mul24 r0, r0, height
# shr r0, r0, 8
# mov r1, fbbase
# :fbfillloop
# mov vw_setup, vdw_setup_0(16, 16, dma_h32(0,0))
# mov vw_setup, vdw_setup_1(0)
# mov vw_addr, r1
# mov -, vw_wait
# sub.setf r0, r0, 1
# brr.anynz -, :fbfillloop
# mov r2, 256
# add r1, r1, r2
# nop

# Initialize our framebuffer pointer
nop; mul24 r0, index, pitch
shl r0, r0, 4
add fbout, fbbase, r0

# Nested loop, down the y direction first
# Each QPU does 16 rows, so start at index * 16
shl row, index, 4

:rowloop

# Each QPU does a whole row at a time.
mov col, 0

# Calculate our y-coordinate
mov r0, row
add r0, r0, elem_num
itof r0, r0
nop;	fmul r0, r0, scale
fadd y0, yorigin, r0

:colloop
mov count, 0  # Count up

# VPM block write setup
# Stride = 1, vertical, laned, 8 bit, start address 0
mov r0, (1 << 12) | (0 << 11) | (1 << 10) | (0 << 8) | (0 << 0)

# Add the start address of our block of the VPM
shl r1, index, 4 # 2 bits for byte address, 2 bits for row address
add vw_setup, r0, r1

:pointloop

# Calculate our x-coordinate
mov r0, col
add r0, r0, count
itof r0, r0
nop; 	fmul r0, r0, scale
fadd x0, xorigin, r0

# Because all 16 points in the vector must go
# round the loop together, we can only bail out
# when all points have gone over limit, so add
# a variable increment and set increment to 0
# when limit reached.

# Use accumulators for important variables
.set x2,  r1
.set y2,  r2
.set res, r3

mov res, 0   # Result will go here
mov x, x0    # Our point
mov y, y0

mov inc, 1   # Increment res by this each time
mov i, iters   # Maximum number of iterations

# We fuse several iterations together and save a couple of
# instructions per iteration. (2 + 9*UNROLL instructions total).
nop;             fmul x2, x, x # Moved to delay slot on loop
:mandelloop
nop;             fmul y2, y, y
fadd r0, x2, y2; fmul y1, x, y
fsub.setf -, 4.0, r0
brr.alln -, :endloop	# All done?

.set UNROLL, 4
.rep i,UNROLL-1
fsub x1, x2, y2
fadd y1, y1, y1; mov.ifn inc, 0
fadd x, x0, x1
fadd y, y0, y1

add res, res, inc; fmul x2, x, x
nop;               fmul y2, y, y
fadd r0, x2, y2;   fmul y1, x, y
fsub.setf -, 4.0, r0
brr.alln -, :endloop	# All done?
.endr

fsub x1, x2, y2
fadd y1, y1, y1; mov.ifn inc, 0

sub.setf i, i, UNROLL # Branch delay slot, but doesn't matter
brr.anynz -, :mandelloop
fadd x, x0, x1
fadd y, y0, y1
add res, res, inc; fmul x2, x, x

:endloop

add count, count, 1
mov r0, iters
sub r0, r0, 1
and vpm, res, r0      # Write out result

.unset x2
.unset y2
.unset res

sub.setf -, count, 16 # Write 16 bytes across (4 words)
brr.anynz -, :pointloop
nop
nop
nop

# Now write out our block. Probably would be easier if
# we wrote horizontal vectors.
mov r0, vdw_setup_0(16, 4, dma_h32(0,0))
and r1, index, 0x3  # Bottom 2 bit of index
shl r1, r1, 2+3     # become top 2 bits of column
add r0, r0, r1
shr r1, index, 2    # Top 2 bits of index
shl r1, r1, 4+4+3   # become bits [5,4] of row
add vw_setup, r0, r1

mov r0, vdw_setup_1(0)
add r0, r0, pitch
sub r0, r0, 16
mov vw_setup, r0

mov vw_addr, fbout
mov -, vw_wait

add col, col, 16
add fbout, fbout, 16
sub.setf -, col, width
brr.anyn -, :colloop
nop
nop
nop

shl r0, nqpus, 4  # Skip over other QPUs output
add row, row,  r0
nop; mul24 r0, width, nqpus
sub.setf -, row, height
brr.anyn -, :rowloop
shl r0, r0, 4
sub r0, r0, width
add fbout, fbout, r0

# Every QPU releases semaphore 1 (ie. increments it)
# and terminates, except for QPU 0,
mov.setf -, index
brr.anynz -, :end
srel -, 1
nop
nop

# which acquires the semaphore nqpus times
mov r0, nqpus
:semloop
sub.setf r0, r0, 1
brr.anynz -, :semloop
sacq -, 1
nop
nop

# and interrupts the host.
mov interrupt, 1;

:end
nop; nop; thrend
nop
nop
