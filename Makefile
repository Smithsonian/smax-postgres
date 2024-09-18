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

# Build everything...
.PHONY: all
all: app $(DOC_TARGETS) check

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
	@echo "  clean         Removes intermediate products."
	@echo "  distclean     Deletes all generated files."
	@echo

# This Makefile depends on the config and build snipplets.
Makefile: config.mk build.mk

# ===============================================================================
# Generic targets and recipes below...
# ===============================================================================

include build.mk

	
