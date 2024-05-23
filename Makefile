# Load the common Makefile definitions...
include $(GLOBALINC)/setup.mk

# PostgresSQL installation directory, including libraries and headers
PGDIR = /usr/pgsql-14

# Uncomment if using timescaleDB < 2.13 (version 2.13 introduced a new interface)
#DFLAGS += -DTIMESCALEDB_OLD=1

# Root path for installation, such as /usr, /usr/local, or /opt
INSTALL_ROOT = /usr/local

# installation path for binaries, by default $(INSTALL_ROOT)/bin
INSTALL_BIN = $(INSTALL_ROOT)/bin

# installation path for config files, by default $(INSTALL_ROOT)/etc
INSTALL_CFG = $(INSTALL_ROOT)/etc/smaxLogger

# Whether to compile with systemd integration
SYSTEMD = 1

BUILD_HOSTS = smax-engdb
TARGET_HOSTS = smax-engdb

IFLAGS += -I$(PGDIR)/include
LDFLAGS += -L$(PGDIR)/lib

SMAX_GRAB = $(OBJ)/smax-collector.o $(SMAX) $(SMAX_LEGACY)

LDFLAGS += $(THREADS) -lpq -lm -lpopt

ifeq ($(SYSTEMD),1) 
  DFLAGS += -DUSE_SYSTEMD=1
  LDFLAGS += -lsystemd
endif

DFLAGS += -DFIX_PYSMAX_STRING_DIM=1

#CFLAGS0 += -g

all: $(BIN)/smaxLogger

# Prerequisites for generic build executables
$(BIN)/smaxLogger: $(OBJ)/smaxLogger.o $(OBJ)/logger-config.o $(OBJ)/postgres-backend.o $(SMAX_GRAB)

.PHONY: install
install: all
	cp $(BIN)/smaxLogger $(INSTALL_BIN)/
	if [ ! -e "$(INSTALL_CFG)" ] ; then \
		mkdir $(INSTALL_CFG); \
	fi
	cp cfg/* $(INSTALL_CFG)/
	if [ "$(SYSTEMD)" == "1" ] ; then \
		cp smaxLogger.service /usr/lib/systemd/system/; \
		systemctl daemon-reload; \
	fi

# Default targets / rules / and dependencies...
include $(GLOBALINC)/recipes.mk
