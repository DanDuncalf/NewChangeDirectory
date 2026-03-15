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
#  Target executable name
# --------------------------------------------------------------------------
TARGET  := NewChangeDirectory.exe

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
#  Source files
# --------------------------------------------------------------------------
SRCDIR  := src
SOURCES := \
    $(SRCDIR)/main.c      \
    $(SRCDIR)/database.c  \
    $(SRCDIR)/scanner.c   \
    $(SRCDIR)/matcher.c   \
    $(SRCDIR)/ui.c

OBJECTS := $(SOURCES:.c=.o)

# --------------------------------------------------------------------------
#  Flags
# --------------------------------------------------------------------------
CFLAGS_COMMON := \
    -std=c11               \
    -Wall                  \
    -Wextra                \
    -Wpedantic             \
    -I$(SRCDIR)            \
    -D_WIN32_WINNT=0x0601  \
    -DWINVER=0x0601

CFLAGS_RELEASE := $(CFLAGS_COMMON) -O2 -DNDEBUG
CFLAGS_DEBUG   := $(CFLAGS_COMMON) -O0 -g3 -DDEBUG

# Link against the Windows subsystem (console) and needed libs
# -mconsole  -- keeps the console window
# -municode  -- Unicode entry point (not needed here but harmless)
LDFLAGS := -mconsole -mthreads -lkernel32 -luser32

# Default to release
CFLAGS ?= $(CFLAGS_RELEASE)

# --------------------------------------------------------------------------
#  Build rules
# --------------------------------------------------------------------------
.PHONY: all debug clean install

all: $(TARGET)

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Build successful: $(TARGET)"
	@echo ""

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# --------------------------------------------------------------------------
#  Dependency includes (optional -- regenerate with make deps)
# --------------------------------------------------------------------------
.PHONY: deps
deps:
	$(CC) -MM $(CFLAGS) $(SOURCES) > .depend 2>/dev/null || true

-include .depend

# --------------------------------------------------------------------------
#  Clean
# --------------------------------------------------------------------------
clean:
	del /f /q $(SRCDIR)\*.o $(TARGET) .depend 2>nul || \
	rm -f $(SRCDIR)/*.o $(TARGET) .depend

# --------------------------------------------------------------------------
#  Install  (copy exe + batch wrapper to INSTALL_DIR)
# --------------------------------------------------------------------------
install: all
	@echo Installing to $(INSTALL_DIR) ...
	copy /y $(TARGET) "$(INSTALL_DIR)\$(TARGET)"
	copy /y ncd.bat   "$(INSTALL_DIR)\ncd.bat"
	@echo Done.  Make sure $(INSTALL_DIR) is on your PATH.
