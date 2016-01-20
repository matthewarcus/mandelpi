EXES := mandel
DEFS :=

VC4ROOT := ../../../..
VC4ASM := $(VC4ROOT)/bin/vc4asm

all : $(EXES)

%.o : %.cpp
	g++ $(DEFS) -MMD -Wall -g -c -o $@ $<

$(EXES) : %: %.o
	g++ -o $@ -Wall $^ -lm 

mandel: mailbox.o

# Can change this eg. with make HEXFILE=transpose.hex
# Might need a make clean
HEXFILE := mandel.hex
DOAPP := DOMANDEL

# Need explicit dependency as -MMD won't cover fresh builds
mandel.o : $(HEXFILE)

mandel.o : DEFS += -DHEXFILE=\"$(HEXFILE)\" -D$(DOAPP)

%.hex : %.qasm
	$(VC4ASM) -V -C $@ $(VC4ROOT)/share/vc4.qinc $<

test: mandel
	@sudo sync
	@sudo ./mandel $(ARGS)

clean :
	rm -f *.hex *.o *.d $(EXES)

-include *.d

.PHONY: test
