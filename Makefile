# ===============================================================================
# WARNING! You should leave this Makefile alone probably
#          To configure the build, you can edit config.mk, or else you export the 
#          equivalent shell variables prior to invoking 'make' to adjust the
#          build configuration. 
# ===============================================================================

include config.mk

# ===============================================================================
# Specific build targets and recipes below...
# ===============================================================================

# Link against necessary system libraries
LDFLAGS += -pthread -lm -lpopt

# Check if there is a doxygen we can run
ifndef DOXYGEN
  DOXYGEN := $(shell which doxygen)
else
  $(shell test -f $(DOXYGEN))
endif

# If there is doxygen, build the API documentation also by default
ifeq ($(.SHELLSTATUS),0)
  DOC_TARGETS += local-dox
else
  $(info WARNING! Doxygen is not available. Will skip 'dox' target) 
endif

ifdef PREFIX
  STAGE=$(PREFIX)/$(DESTDIR)
else
  STAGE=$(DESTDIR)
endif

# Build everything...
.PHONY: all
all: deploy check

.PHONY: deploy
deploy: app $(DOC_TARGETS)

# Remove intermediates
.PHONY: clean
clean:
	rm -f $(OBJECTS) README-smax-postgres.md gmon.out

# Remove all generated files
.PHONY: distclean
distclean: clean
	rm -f Doxyfile.local $(BIN)/smax-postgress

# ----------------------------------------------------------------------------
# The nitty-gritty stuff below
# ----------------------------------------------------------------------------

SOURCES = $(SRC)/smax-postgres.c $(SRC)/logger-config.c $(SRC)/postgres-backend.c $(SRC)/smax-collector.o

# Generate a list of object (obj/*.o) files from the input sources
OBJECTS := $(subst $(SRC),$(OBJ),$(SOURCES))
OBJECTS := $(subst .c,.o,$(OBJECTS))

.PHONY: app
app: $(BIN)/smax-postgres

$(BIN)/smax-postgres: $(OBJECTS) | $(BIN)

README-smax-postgres.md: README.md
	LINE=`sed -n '/\# /{=;q;}' $<` && tail -n +$$((LINE+2)) $< > $@

dox: README-smax-postgres.md

.INTERMEDIATE: Doxyfile.local
Doxyfile.local: Doxyfile Makefile
	sed "s:resources/header.html::g" $< > $@
	sed -i "s:^TAGFILES.*$$:TAGFILES = :g" $@

# Local documentation without specialized headers. The resulting HTML documents do not have
# Google Search or Analytics tracking info.
.PHONY: local-dox
local-dox: README-smax-postgres.md Doxyfile.local
	doxygen Doxyfile.local

CONFIG := "cfg/smax-postgres.cfg"

.PHONY: install
install: deploy
	@echo "Installing under $(STAGE)."
	
	@mkdir -p $(STAGE)/bin
	@install -m 755 $(BIN)/smax-postgres $(STAGE)/bin/
	
	@mkdir -p $(STAGE)/etc
	@if [ ! -e $(PREFIX)/etc/smax-postgres.cfg ] ; then \
	  install -m 644 cfg/smax-postgres.cfg $(PREFIX)/etc/smax-postgres.cfg ; \
	fi
	
	@if [ $(SYSTEMD) -ne 0 ] ; then \
	  mkdir -p $(STAGE)/etc/systemd/system ; \
	  install -m 644 smax-postgres.service $(STAGE)/etc/systemd/system/ ; \
  	  sed -i "s:/usr/:$(STAGE):g" $(STAGE)/etc/systemd/system/smax-postgres.service ; \
	fi
	
	@mkdir -p $(STAGE)/share/doc/smax-postgres
	@install -m 644 LICENSE $(STAGE)/share/doc/smax-postgres/
	@install -m 644 README-smax-postgres.md $(STAGE)/share/doc/smax-postgres/README.md
	@install -m 644 CHANGELOG.md $(STAGE)/share/doc/smax-postgres/
	
	@if [ -e apidoc/html/index.html ] ; then \
	  mkdir -p $(STAGE)/share/doc/smax-postgres/html/search ; \
	  install -m 644 -D apidoc/html/* $(STAGE)/share/doc/html/smax-postgres/ ; \
	  install -m 644 -D apidoc/html/search/* $(STAGE)/share/doc/smax-postgres/html/search/ ; \
	fi

.PHONY: install-sma
install-sma: CONFIG := "cfg/smax-postgres.cfg.sma"
install-sma: install

# Built-in help screen for `make help`
.PHONY: help
help:
	@echo
	@echo "Syntax: make [target]"
	@echo
	@echo "The following targets are available:"
	@echo
	@echo "  app           'smax-postgres' application."
	@echo "  local-dox     Compiles local HTML API documentation using 'doxygen'."
	@echo "  check         Performs static analysis with 'cppcheck'."
	@echo "  all           All of the above."
	@echo "  install       Install (may require sudo)"
	@echo "  install-sma   Install at the SMA (with sudo)"
	@echo "  clean         Removes intermediate products."
	@echo "  distclean     Deletes all generated files."
	@echo

# This Makefile depends on the config and build snipplets.
Makefile: config.mk build.mk

# ===============================================================================
# Generic targets and recipes below...
# ===============================================================================

include build.mk

	
