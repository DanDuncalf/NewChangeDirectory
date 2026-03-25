# ===========================================================================
#  Makefile  --  NewChangeDirectory (NCD) for Windows 64-bit
#  Toolchain: MinGW-w64  (gcc / x86_64-w64-mingw32-gcc)
#
#  Usage:
#    make            -- build release binary
#    make debug      -- build with debug symbols and ASAN-style checks
#    make clean      -- remove build artefacts
#    make install    -- copy ncd.bat + exe to INSTALL_DIR (set below)
#
#  Tested with:
#    x86_64-w64-mingw32-gcc 12+  (MSYS2 / mingw-w64)
#    Native Windows gcc from WinLibs / LLVM-MinGW
# ===========================================================================

# --------------------------------------------------------------------------
#  Target executable names
# --------------------------------------------------------------------------
TARGET  := NewChangeDirectory.exe
SERVICE := NCDService.exe

# --------------------------------------------------------------------------
#  Install directory (edit or override on the command line)
#  e.g.  make install INSTALL_DIR=C:/tools
# --------------------------------------------------------------------------
INSTALL_DIR ?= C:/Windows/System32

# --------------------------------------------------------------------------
#  Compiler  (override if cross-compiling: make CC=x86_64-w64-mingw32-gcc)
# --------------------------------------------------------------------------
CC ?= gcc

# --------------------------------------------------------------------------
#  Source directories
# --------------------------------------------------------------------------
SRCDIR  := src
SHAREDDIR := ../shared
OBJDIR  := obj

# --------------------------------------------------------------------------
#  Common source files (shared between main and service)
# --------------------------------------------------------------------------
COMMON_SOURCES := \
    $(SRCDIR)/database.c  \
    $(SRCDIR)/scanner.c   \
    $(SRCDIR)/matcher.c   \
    $(SRCDIR)/platform.c  \
    $(SRCDIR)/cli.c       \
    $(SRCDIR)/result.c    \
    $(SRCDIR)/state_backend_local.c \
    $(SRCDIR)/state_backend_service.c \
    $(SRCDIR)/shared_state.c \
    $(SRCDIR)/shm_platform_win.c \
    $(SRCDIR)/control_ipc_win.c \
    $(SRCDIR)/service_state.c \
    $(SRCDIR)/service_publish.c \
    $(SHAREDDIR)/platform.c \
    $(SHAREDDIR)/strbuilder.c \
    $(SHAREDDIR)/common.c

# Main executable sources
MAIN_SOURCES := \
    $(SRCDIR)/main.c      \
    $(SRCDIR)/ui.c        \
    $(COMMON_SOURCES)

# Service executable sources
SERVICE_SOURCES := \
    $(SRCDIR)/service_main.c \
    $(COMMON_SOURCES)

MAIN_OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(MAIN_SOURCES)))
SERVICE_OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(SERVICE_SOURCES)))

# VPATH to find source files
VPATH := $(SRCDIR):$(SHAREDDIR)

# --------------------------------------------------------------------------
#  Flags
# --------------------------------------------------------------------------
CFLAGS_COMMON := \
    -std=c11               \
    -Wall                  \
    -Wextra                \
    -Wpedantic             \
    -I$(SRCDIR)            \
    -I$(SHAREDDIR)         \
    -D_WIN32_WINNT=0x0601  \
    -DWINVER=0x0601

CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG
CFLAGS_DEBUG   := $(CFLAGS_COMMON) -O0 -g3 -DDEBUG

# Link against the Windows subsystem (console) and needed libs
LDFLAGS := -mconsole -mthreads -lkernel32 -luser32 -ladvapi32 -lshlwapi

# Default to release
CFLAGS ?= $(CFLAGS_RELEASE)

# --------------------------------------------------------------------------
#  Build rules
# --------------------------------------------------------------------------
.PHONY: all debug clean install

all: $(TARGET) $(SERVICE)

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: all

$(TARGET): $(MAIN_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Build successful: $(TARGET)"
	@echo ""

$(SERVICE): $(SERVICE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Build successful: $(SERVICE)"
	@echo ""

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	@mkdir -p $(OBJDIR) 2>/dev/null || true

# --------------------------------------------------------------------------
#  Dependency includes (optional -- regenerate with make deps)
# --------------------------------------------------------------------------
.PHONY: deps
deps:
	$(CC) -MM $(CFLAGS) $(MAIN_SOURCES) $(SERVICE_SOURCES) > .depend 2>/dev/null || true

-include .depend

# --------------------------------------------------------------------------
#  Clean
# --------------------------------------------------------------------------
clean:
	@if exist $(OBJDIR) rmdir /s /q $(OBJDIR) 2>nul
	@rm -rf $(OBJDIR) $(TARGET) $(SERVICE) .depend 2>/dev/null || true

# --------------------------------------------------------------------------
#  Install  (copy exe + batch wrapper to INSTALL_DIR)
# --------------------------------------------------------------------------
install: all
	@echo Installing to $(INSTALL_DIR) ...
	copy /y $(TARGET) "$(INSTALL_DIR)\$(TARGET)"
	copy /y $(SERVICE) "$(INSTALL_DIR)\$(SERVICE)" 2>nul || echo "  (Service not copied)"
	copy /y ncd.bat   "$(INSTALL_DIR)\ncd.bat"
	@echo Done.  Make sure $(INSTALL_DIR) is on your PATH.
