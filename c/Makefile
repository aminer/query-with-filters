###############################################################################
##  SETTINGS                                                                 ##
###############################################################################

OS = $(shell uname)
ARCH = $(shell uname -m)
PLATFORM = $(OS)-$(ARCH)

CFLAGS = -std=gnu99 -g -Wall -fPIC -O3
CFLAGS += -fno-common -fno-strict-aliasing
CFLAGS += -march=nocona -DMARCH_$(ARCH)
CFLAGS += -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE

ifeq ($(OS),Darwin)
  CFLAGS += -D_DARWIN_UNLIMITED_SELECT
  TARGET_LIB= /usr/local/lib
else
  CFLAGS += -rdynamic
  TARGET_LIB= /usr/lib
endif

CFLAGS += -I/usr/local/include -I/usr/local/include/ck
CFLAGS += -I src/include

LDFLAGS = -lssl -lcrypto -lpthread
ifneq ($(OS),Darwin)
  LDFLAGS += -lrt -ldl
endif

LDFLAGS += -lm

ifeq ($(OS),Darwin)
  CC = clang
else
  CC = gcc
endif

###############################################################################
##  OBJECTS                                                                  ##
###############################################################################

OBJECTS = example.o example_utils.o

###############################################################################
##  MAIN TARGETS                                                             ##
###############################################################################

all: build

.PHONY: build
build: target/example

.PHONY: clean
clean:
	@rm -rf target

target:
	mkdir $@

target/obj: | target
	mkdir $@

target/obj/example_utils.o: src/main/example_utils.c | target/obj
	$(CC) $(CFLAGS) -o $@ -c $^

target/obj/%.o: src/main/%.c | target/obj
	$(CC) $(CFLAGS) -o $@ -c $^

target/example: $(addprefix target/obj/,$(OBJECTS)) | target
	$(CC) -o $@ $^ $(TARGET_LIB)/libaerospike.a $(LDFLAGS)

.PHONY: run
run: build
	./target/example

.PHONY: valgrind
valgrind: build
	valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --num-callers=20 --track-fds=yes -v ./target/example