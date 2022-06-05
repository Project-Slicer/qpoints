QEMU_DIR ?=
DEBUG ?= 0

ifeq ($(DEBUG),0)
DEBUG_FLAGS ?= -O3
else
DEBUG_FLAGS ?= -g
endif

GLIB_INC ?=`pkg-config --cflags glib-2.0`
CXXFLAGS ?= $(DEBUG_FLAGS) -Wall -std=c++14 -march=native -iquote $(QEMU_DIR)/include/qemu/ $(GLIB_INC)

all: libbbv.so

libbbv.so: bbv.cc
	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl -lrt -lz

clean:
	rm -f *.o libbbv.so
