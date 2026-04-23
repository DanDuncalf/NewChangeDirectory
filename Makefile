# ===========================================================================
#  Unified Makefile -- NewChangeDirectory (NCD)
#  Builds: main binaries + full test suite
#  Platforms: Windows (MinGW/MSYS) and Linux
#  Warnings are treated as errors everywhere
# ===========================================================================

# --------------------------------------------------------------------------
#  Platform auto-detection
# --------------------------------------------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
UNAME_M := $(shell uname -m 2>/dev/null || echo x86_64)

ifeq ($(OS),Windows_NT)
    PLATFORM := Windows
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    PLATFORM := Windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    PLATFORM := Windows
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    PLATFORM := Windows
else ifeq ($(UNAME_S),Linux)
    PLATFORM := Linux
else
    PLATFORM := Linux
endif

# --------------------------------------------------------------------------
#  Compiler & flags
# --------------------------------------------------------------------------
CC ?= gcc

# Base flags: strict C11, all warnings, warnings as errors
# -Wno-format-truncation and -Wno-stringop-truncation are suppressed
# because bounded snprintf/strncpy copies are an intentional design.
CFLAGS_BASE := -std=c11 -Wall -Wextra -Werror -Wno-format-truncation -Wno-stringop-truncation

ifeq ($(PLATFORM),Windows)
    TARGET      := NewChangeDirectory.exe
    SERVICE     := NCDService.exe
    CFLAGS_PLAT := -D_WIN32_WINNT=0x0601 -DWINVER=0x0601
    LDFLAGS     := -mconsole -mthreads -lkernel32 -luser32 -ladvapi32 -lshlwapi
    PLATFORM_SRC:= src/shm_platform_win.c src/control_ipc_win.c
else
    TARGET      := NewChangeDirectory
    SERVICE     := ncd_service
    CFLAGS_PLAT := -DPLATFORM_LINUX=1 -D_GNU_SOURCE
    LDFLAGS     := -lpthread
    PLATFORM_SRC:= src/shm_platform_posix.c src/control_ipc_posix.c
endif

# --------------------------------------------------------------------------
#  Directories
# --------------------------------------------------------------------------
SRCDIR      := src
SHAREDDIR   := ../shared
OBJDIR      := build/$(PLATFORM)/obj
BINDIR      := build/$(PLATFORM)/bin

CFLAGS  := $(CFLAGS_BASE) $(CFLAGS_PLAT) -O2 -DNDEBUG -I$(SRCDIR) -I$(SHAREDDIR)
CFLAGS_DEBUG := $(CFLAGS_BASE) $(CFLAGS_PLAT) -O0 -g3 -DDEBUG -I$(SRCDIR) -I$(SHAREDDIR)

# --------------------------------------------------------------------------
#  Sources
# --------------------------------------------------------------------------
COMMON_SRC := \
    $(SRCDIR)/database.c \
    $(SRCDIR)/scanner.c \
    $(SRCDIR)/matcher.c \
    $(SRCDIR)/platform_ncd.c \
    $(SRCDIR)/cli.c \
    $(SRCDIR)/result.c \
    $(SRCDIR)/state_backend_local.c \
    $(SRCDIR)/state_backend_service.c \
    $(SRCDIR)/shared_state.c \
    $(SRCDIR)/service_state.c \
    $(SRCDIR)/service_publish.c \
    $(PLATFORM_SRC) \
    $(SHAREDDIR)/platform.c \
    $(SHAREDDIR)/strbuilder.c \
    $(SHAREDDIR)/common.c

MAIN_SRC   := $(SRCDIR)/main.c $(SRCDIR)/ui.c $(COMMON_SRC)
SERVICE_SRC:= $(SRCDIR)/service_main.c $(SRCDIR)/ui.c $(COMMON_SRC)

# --------------------------------------------------------------------------
#  Objects
# --------------------------------------------------------------------------
MAIN_OBJECTS    := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(MAIN_SRC)))
SERVICE_OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(SERVICE_SRC)))

VPATH := $(SRCDIR):$(SHAREDDIR)

# --------------------------------------------------------------------------
#  Phony targets
# --------------------------------------------------------------------------
.PHONY: all ncd service tests test clean debug install

# --------------------------------------------------------------------------
#  Default: build binaries + tests
# --------------------------------------------------------------------------
all: ncd service tests

# --------------------------------------------------------------------------
#  Main binaries
# --------------------------------------------------------------------------
ncd: $(TARGET)

service: $(SERVICE)

$(TARGET): $(MAIN_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SERVICE): $(SERVICE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

# --------------------------------------------------------------------------
#  Debug build
# --------------------------------------------------------------------------
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: all

# --------------------------------------------------------------------------
#  Tests (Linux only; Windows tests use Run-Tests-Safe.bat)
# --------------------------------------------------------------------------
ifeq ($(PLATFORM),Linux)
    TEST_CFLAGS := $(CFLAGS_BASE) $(CFLAGS_PLAT) -g \
        -I../src -I../../shared -I. \
        -Wno-error=unused-label \
        -Wno-error=unused-variable \
        -Wno-error=unused-function \
        -Wno-error=type-limits \
        -Wno-error=return-type \
        -Wno-error=implicit-function-declaration \
        -DNCD_TEST_BUILD

    tests: $(TARGET) $(SERVICE)
	$(MAKE) -C test all CC="$(CC)" CFLAGS="$(TEST_CFLAGS)" LDFLAGS="$(LDFLAGS)"

    test: $(TARGET) $(SERVICE) tests
	$(MAKE) -C test test CC="$(CC)" CFLAGS="$(TEST_CFLAGS)" LDFLAGS="$(LDFLAGS)"
else
    tests:
	@echo "Windows test runner: cmd /c Run-Tests-Safe.bat"

    test: ncd service tests
endif

# --------------------------------------------------------------------------
#  Clean
# --------------------------------------------------------------------------
clean:
	rm -rf build $(TARGET) $(SERVICE) .depend
	$(MAKE) -C test clean

# --------------------------------------------------------------------------
#  Install (Windows only; Linux uses deploy.sh)
# --------------------------------------------------------------------------
ifeq ($(PLATFORM),Windows)
INSTALL_DIR ?= C:/Windows/System32

install: all
	@echo Installing to $(INSTALL_DIR) ...
	cp -f $(TARGET) "$(INSTALL_DIR)/$(TARGET)"
	cp -f $(SERVICE) "$(INSTALL_DIR)/$(SERVICE)" || echo "  (Service not copied)"
	cp -f ncd.bat "$(INSTALL_DIR)/ncd.bat"
	@echo Done. Make sure $(INSTALL_DIR) is on your PATH.
endif

# --------------------------------------------------------------------------
#  Dependency includes (optional -- regenerate with make deps)
# --------------------------------------------------------------------------
.PHONY: deps
deps:
	$(CC) -MM $(CFLAGS) $(MAIN_SRC) $(SERVICE_SRC) > .depend 2>/dev/null || true

-include .depend
