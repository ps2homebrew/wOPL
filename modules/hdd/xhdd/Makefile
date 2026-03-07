IOP_BIN = xhdd.irx
IOP_OBJS = xhdd.o xatad.o hdpro.o imports.o

OLD_SDK ?= 0

IOP_CFLAGS += -DOLD_SDK=$(OLD_SDK)

IOP_INCS += -I../common

include $(PS2SDK)/Defs.make
include ../../Rules.bin.make
include $(PS2SDK)/samples/Makefile.iopglobal
