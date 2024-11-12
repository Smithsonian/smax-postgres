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

# For deployment, the app and documentation
.PHONY: distro
distro: app $(DOC_TARGETS)

# Build just the app
.PHONY: app
app: $(BIN)/smax-postgres

# Build everything...
.PHONY: all
all: deploy check

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


# Default values for install locations
# See https://www.gnu.org/prep/standards/html_node/Directory-Variables.html 
prefix ?= /usr
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
sysconfdir ?= $(prefix)/etc
systemddir ?= $(sysconfdir)/systemd/system
datarootdir ?= $(prefix)/share
datadir ?= $(datarootdir)
mydatadir ?= $(datadir)/smax-postgres
docdir ?= $(datarootdir)/doc/smax-postgres
htmldir ?= $(docdir)/html


CONFIG := "cfg/smax-postgres.cfg"

.PHONY: install-sma
install-sma: CONFIG := "cfg/smax-postgres.cfg.sma"
install-sma: install


.PHONY: install
install: install-bin install-cfg install-systemd install-doc

.PHONY: install-bin
install-bin: app
	@echo "installing executable under $(bindir)."
	@install -d $(bindir)
	@install -m 755 $(BIN)/smax-postgres $(bindir)/

.PHONY: install-cfg
	@if [ ! -e $(sysconfdir)/smax-postgres.cfg ] ; then \
	  echo "installing configuration file under $(sysconfdir)." ; \
	  install -d $(sysconfdir) ; \
	  install -m 644 cfg/smax-postgres.cfg $(sysconfdir)/smax-postgres.cfg ; \
	fi

.PHONY: install-systemd
install-systemd:
	@if [ $(SYSTEMD) -ne 0 ] ; then \
	  echo "installing systemd unit file under $(systemddir)." ; \
	  mkdir -p $(systemddir) ; \
	  install -m 644 smax-postgres.service $(systemddir) ; \
  	  sed -i "s:/usr/:$(prefix):g" $(systemddir)/smax-postgres.service ; \
	fi

.PHONY: install-doc
install-doc: install-apidoc $(DOC_TARGETS)
	@echo "installing docs under $(docdir)."
	@install -d $(docdir)
	@install -m 644 LICENSE $(docdir)
	@install -m 644 README-smax-postgres.md $(docdir)/README.md
	@install -m 644 CHANGELOG.md $(docdir)

.PHONY: install-apidoc
install-apidoc: $(DOC_TARGETS)
	@if [ -e apidoc/html/index.html ] ; then \
	  echo "installing API docs under $(htmldir)." ; \
	  install -d $(htmldir)/search ; \
	  install -m 644 -D apidoc/html/* $(htmldir)/smax-postgres/ ; \
	  install -m 644 -D apidoc/html/search/* $(htmldir)/search/ ; \
	fi


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
	@echo "  install       Install components (e.g. 'make prefix=<path> install')"
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


