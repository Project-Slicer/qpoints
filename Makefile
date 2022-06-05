QEMU_DIR ?=
DEBUG ?= 0
MEM_START ?= 0x80000000

ifeq ($(DEBUG),0)
DEBUG_FLAGS ?= -O3
else
DEBUG_FLAGS ?= -g
endif

GLIB_INC ?= $(shell pkg-config --cflags glib-2.0)
QEMU_INC ?= -iquote $(QEMU_DIR)/include/qemu/
CXXFLAGS ?= $(DEBUG_FLAGS) -Wall -std=c++14 -march=native $(QEMU_INC) $(GLIB_INC) -DMEM_START=$(MEM_START)

all: libbbv.so

libbbv.so: bbv.cc
	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl -lrt -lz

clean:
	rm -f *.o libbbv.so
