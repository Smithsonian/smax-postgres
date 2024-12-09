# ===========================================================================
# Generic configuration options for building smax-postgres
#
# You can include this snipplet in your Makefile also.
# ============================================================================

# Whether to compile with systemd integration (needs systemd development
# files, i.e. sd-daemon.h and libsystemd.so).
SYSTEMD ?= 1

# The PostgreSQL installation directory containing C headers and shared
# libraries (libpq.so)
#PGDIR ?= /usr/pgsql-16

# Location under which the Smithsonian/xchange library is installed.
# (I.e., the root directory under which there is an include/ directory
# that contains xchange.h, and a lib/ or lib64/ directory that contains
# libxchange.so
XCHANGE ?= /usr

# Location under which the Smithsonian/redisx library is installed.
# (I.e., the root directory under which there is an include/ directory
# that contains redisx.h, and a lib/ or lib64/ directory that contains
# libredisx.so
REDISX ?= /usr

# Location under which the Smithsonian/smax-clib library is installed.
# (I.e., the root directory under which there is an include/ directory
# that contains smax.h, and a lib/ or lib64/ directory that contains
# libsmax.so
SMAXLIB ?= /usr

# Folders in which sources and header files are located, respectively
SRC ?= src
INC ?= include

# Folders for compiled objects, libraries, and binaries, respectively 
OBJ ?= obj
LIB ?= lib
BIN ?= bin

# Compiler: use gcc by default
CC ?= gcc

# Add include/ directory
CPPFLAGS += -I$(INC)

# Uncomment if using timescaleDB < 2.13 (version 2.13 introduced a new interface)
#CPPFLAGS += -DTIMESCALEDB_OLD=1

# Base compiler options (if not defined externally...)
CFLAGS ?= -g -Os -Wall -std=c99

# Extra warnings (not supported on all compilers)
#CFLAGS += -Wextra

# Link against math libs (for e.g. isnan())
LDFLAGS ?= -lm

# cppcheck options for 'check' target
CHECKOPTS ?= --enable=performance,warning,portability,style --language=c \
            --error-exitcode=1 --std=c11

# Add-on ccpcheck options
CHECKOPTS += --inline-suppr $(CHECKEXTRA)

# Exhaustive checking for newer cppcheck
#CHECKOPTS += --check-level=exhaustive

# Specific Doxygen to use if not the default one
#DOXYGEN ?= /opt/bin/doxygen

# ============================================================================
# END of user config section. 
#
# Below are some generated constants based on the one that were set above
# ============================================================================

# Make sure we can locate the PostgreSQL headers / libraries
ifdef PGDIR
  # Search the selected Postgres directory
  CPPFLAGS += -I$(PGDIR)/include
  LDFLAGS += -L$(PGDIR)/lib
else
  # Check if libpq-fe.h is in unusual location (bloody Debian...)
  $(shell test -e /usr/include/postgresql)
  ifeq ($(.SHELLSTATUS),0)
    CPPFLAGS += -I/usr/include/postgresql
  endif
endif

# Always link against dependencies
LDFLAGS += -lm -lsmax -lredisx -lxchange -lpq

ifeq ($(SYSTEMD),1) 
  DFLAGS += -DUSE_SYSTEMD=1
  LDFLAGS += -lsystemd
endif

# Search for libraries under LIB
ifneq ($(findstring $(LIB),$(LD_LIBRARY_PATH)),$LIB)
  LDFLAGS += -L$(LIB)
  LD_LIBRARY_PATH := $(LIB):$(LD_LIBRARY_PATH)
endif

# Compile and link against a specific PostgreSQL library (if defined)
ifdef PGDIR
  CPPFLAGS += -I$(PGDIR)/include
  LDFLAGS += -L$(PGDIR)/lib
  LD_LIBRARY_PATH := $(PGDIR)/lib:$(LD_LIBRARY_PATH)
endif

# Compile and link against a specific smax library (if defined)
ifdef SMAXLIB
  CPPFLAGS += -I$(SMAXLIB)/include
  LDFLAGS += -L$(SMAXLIB)/lib
  LD_LIBRARY_PATH := $(SMAXLIB)/lib:$(LD_LIBRARY_PATH)
endif

# Compile and link against a specific redisx library (if defined)
ifdef REDISX
  CPPFLAGS += -I$(REDISX)/include
  LDFLAGS += -L$(REDISX)/lib
  LD_LIBRARY_PATH := $(REDISX)/lib:$(LD_LIBRARY_PATH)
endif

# Compile and link against a specific xchange library (if defined)
ifdef XCHANGE
  CPPFLAGS += -I$(XCHANGE)/include
  LDFLAGS += -L$(XCHANGE)/lib
  LD_LIBRARY_PATH := $(XCHANGE)/lib:$(LD_LIBRARY_PATH)
endif



# Search for files in the designated locations
vpath %.h $(INC)
vpath %.c $(SRC)
vpath %.o $(OBJ)
vpath %.d dep 

