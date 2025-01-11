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
LDFLAGS += -lpthread -lm -lpopt

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

# Regression testing
.PHONY: test
test:

# 'test' + 'analyze'
.PHONY: check
check: test analyze

# Static code analysis via Facebook's infer
.PHONY: infer
infer: clean
	infer run -- $(MAKE) app

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

# Some standard GNU targets, that should always exist...
.PHONY: html
html: local-dox

.PHONY: dvi
dvi:

.PHONY: ps
ps:

.PHONY: pdf
pdf:

# Default values for install locations
# See https://www.gnu.org/prep/standards/html_node/Directory-Variables.html 
prefix ?= /usr
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
sysconfdir ?= $(prefix)/etc
systemddir ?= $(sysconfdir)/systemd/system
datarootdir ?= $(prefix)/share
datadir ?= $(datarootdir)
mandir ?= $(datarootdir)/man
mydatadir ?= $(datadir)/smax-postgres
docdir ?= $(datarootdir)/doc/smax-postgres
htmldir ?= $(docdir)/html

# Standard install commands
INSTALL_PROGRAM ?= install
INSTALL_DATA ?= install -m 644

CONFIG := "cfg/smax-postgres.cfg"

.PHONY: install-sma
install-sma: CONFIG := "cfg/smax-postgres.cfg.sma"
install-sma: install

.PHONY: install
install: install-bin install-man install-cfg install-systemd install-doc

.PHONY: install-bin
install-bin:
ifneq ($(wildcard $(BIN)/*),)
	@echo "installing executable(s) under $(DESTDIR)$(bindir)."
	install -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -D $(BIN)/* $(DESTDIR)$(bindir)/
else
	@echo "WARNING! Skipping bin install: needs 'app'"
endif

.PHONY: install-man
install-man:
	@echo "installing man pages under $(DESTDIR)$(mandir)."
	@install -d $(DESTDIR)$(mandir)/man1
	$(INSTALL_DATA) -D man/man1/* $(DESTDIR)$(mandir)/man1/

.PHONY: install-cfg
ifeq ($(wildcard $(DESTDIR)$(sysconfdir)/smax-postgres.cfg),)
	@echo "installing configuration file under $(DESTDIR)$(sysconfdir)."
	install -d $(DESTDIR)$(sysconfdir)
	$(INSTALL_DATA) cfg/smax-postgres.cfg $(DESTDIR)$(sysconfdir)/smax-postgres.cfg
else
	@echo "WARNING! Will not override existing $(DESTDIR)$(sysconfdir)/smax-postgres.cfg"
endif

.PHONY: install-systemd
install-systemd:
ifneq ($(SYSTEMD), 0)
	@echo "installing systemd unit file under $(DESTDIR)$(systemddir)."
	mkdir -p $(DESTDIR)$(systemddir)
	$(INSTALL_DATA) smax-postgres.service $(DESTDIR)$(systemddir)/
	sed -i "s:-VERSION:-$(PGVER)/:g" $(DESTDIR)$(systemddir)/smax-postgres.service
	sed -i "s:/usr/:$(prefix)/:g" $(DESTDIR)$(systemddir)/smax-postgres.service
endif

.PHONY: install-doc
install-doc: install-html
	@echo "installing docs under $(DESTDIR)$(docdir)."
	@install -d $(DESTDIR)$(docdir)
	$(INSTALL_DATA) LICENSE $(DESTDIR)$(docdir)
	$(INSTALL_DATA) README-smax-postgres.md $(DESTDIR)$(docdir)/README.md
	$(INSTALL_DATA) CHANGELOG.md $(DESTDIR)$(docdir)

.PHONY: install-html
install-html:
ifneq ($(wildcard apidoc/html/search/*),)
	@echo "installing API docs under $(DESTDIR)$(htmldir)."
	install -d $(DESTDIR)$(htmldir)/search
	$(INSTALL_DATA) -D apidoc/html/search/* $(DESTDIR)$(htmldir)/search/
	$(INSTALL_DATA) -D apidoc/html/*.* $(DESTDIR)$(htmldir)/
else
	@echo "WARNING! Skipping apidoc install: needs doxygen and 'local-dox'"
endif

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
	@echo "  analyze       Performs static analysis with 'cppcheck'."
	@echo "  all           All of the above."
	@echo "  distro        shared libs and documentation (default target)."
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


