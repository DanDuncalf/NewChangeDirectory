import ctypes
from ctypes import wintypes
import struct
import sys

kernel32 = ctypes.windll.kernel32

# Constants
DEBUG_PROCESS = 0x00000001
DEBUG_ONLY_THIS_PROCESS = 0x00000002
INFINITE = 0xFFFFFFFF
EXCEPTION_ACCESS_VIOLATION = 0xC0000005
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
DBG_CONTINUE = 0x00010002

ULONG_PTR = ctypes.c_ulonglong

class EXCEPTION_RECORD(ctypes.Structure):
    pass

EXCEPTION_RECORD._fields_ = [
    ("ExceptionCode", wintypes.DWORD),
    ("ExceptionFlags", wintypes.DWORD),
    ("ExceptionRecord", ctypes.POINTER(EXCEPTION_RECORD)),
    ("ExceptionAddress", wintypes.LPVOID),
    ("NumberParameters", wintypes.DWORD),
    ("ExceptionInformation", ULONG_PTR * 15),
]

class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("ExceptionRecord", EXCEPTION_RECORD),
        ("dwFirstChance", wintypes.DWORD),
    ]

class CREATE_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("hThread", wintypes.HANDLE),
        ("lpThreadLocalBase", wintypes.LPVOID),
        ("lpStartAddress", wintypes.LPVOID),
    ]

class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("hFile", wintypes.HANDLE),
        ("hProcess", wintypes.HANDLE),
        ("hThread", wintypes.HANDLE),
        ("lpBaseOfImage", wintypes.LPVOID),
        ("dwDebugInfoFileOffset", wintypes.DWORD),
        ("nDebugInfoSize", wintypes.DWORD),
        ("lpThreadLocalBase", wintypes.LPVOID),
        ("lpStartAddress", wintypes.LPVOID),
        ("lpImageName", wintypes.LPVOID),
        ("fUnicode", wintypes.WORD),
    ]

class EXIT_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("dwExitCode", wintypes.DWORD),
    ]

class EXIT_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("dwExitCode", wintypes.DWORD),
    ]

class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("hFile", wintypes.HANDLE),
        ("lpBaseOfDll", wintypes.LPVOID),
        ("dwDebugInfoFileOffset", wintypes.DWORD),
        ("nDebugInfoSize", wintypes.DWORD),
        ("lpImageName", wintypes.LPVOID),
        ("fUnicode", wintypes.WORD),
    ]

class UNLOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", wintypes.LPVOID),
    ]

class OUTPUT_DEBUG_STRING_INFO(ctypes.Structure):
    _fields_ = [
        ("lpDebugStringData", wintypes.LPSTR),
        ("fUnicode", wintypes.WORD),
        ("nDebugStringLength", wintypes.WORD),
    ]

class RIP_INFO(ctypes.Structure):
    _fields_ = [
        ("dwError", wintypes.DWORD),
        ("dwType", wintypes.DWORD),
    ]

class DEBUG_EVENT_U(ctypes.Union):
    _fields_ = [
        ("Exception", EXCEPTION_DEBUG_INFO),
        ("CreateThread", CREATE_THREAD_DEBUG_INFO),
        ("CreateProcessInfo", CREATE_PROCESS_DEBUG_INFO),
        ("ExitThread", EXIT_THREAD_DEBUG_INFO),
        ("ExitProcess", EXIT_PROCESS_DEBUG_INFO),
        ("LoadDll", LOAD_DLL_DEBUG_INFO),
        ("UnloadDll", UNLOAD_DLL_DEBUG_INFO),
        ("DebugString", OUTPUT_DEBUG_STRING_INFO),
        ("RipInfo", RIP_INFO),
    ]

class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [
        ("dwDebugEventCode", wintypes.DWORD),
        ("dwProcessId", wintypes.DWORD),
        ("dwThreadId", wintypes.DWORD),
        ("u", DEBUG_EVENT_U),
    ]

DWORD64 = ctypes.c_ulonglong

class CONTEXT64(ctypes.Structure):
    _pack_ = 16
    _fields_ = [
        ("P1Home", DWORD64),
        ("P2Home", DWORD64),
        ("P3Home", DWORD64),
        ("P4Home", DWORD64),
        ("P5Home", DWORD64),
        ("P6Home", DWORD64),
        ("ContextFlags", wintypes.DWORD),
        ("MxCsr", wintypes.DWORD),
        ("SegCs", wintypes.WORD),
        ("SegDs", wintypes.WORD),
        ("SegEs", wintypes.WORD),
        ("SegFs", wintypes.WORD),
        ("SegGs", wintypes.WORD),
        ("SegSs", wintypes.WORD),
        ("EFlags", wintypes.DWORD),
        ("Dr0", DWORD64),
        ("Dr1", DWORD64),
        ("Dr2", DWORD64),
        ("Dr3", DWORD64),
        ("Dr6", DWORD64),
        ("Dr7", DWORD64),
        ("Rax", DWORD64),
        ("Rcx", DWORD64),
        ("Rdx", DWORD64),
        ("Rbx", DWORD64),
        ("Rsp", DWORD64),
        ("Rbp", DWORD64),
        ("Rsi", DWORD64),
        ("Rdi", DWORD64),
        ("R8", DWORD64),
        ("R9", DWORD64),
        ("R10", DWORD64),
        ("R11", DWORD64),
        ("R12", DWORD64),
        ("R13", DWORD64),
        ("R14", DWORD64),
        ("R15", DWORD64),
        ("Rip", DWORD64),
        ("FltSave", ctypes.c_ubyte * 512),
        ("VectorRegister", ctypes.c_ubyte * 256),
        ("VectorControl", DWORD64),
        ("DebugControl", DWORD64),
        ("LastBranchToRip", DWORD64),
        ("LastBranchFromRip", DWORD64),
        ("LastExceptionToRip", DWORD64),
        ("LastExceptionFromRip", DWORD64),
    ]

CONTEXT_FULL = 0x10007

class STARTUPINFOA(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("lpReserved", wintypes.LPSTR),
        ("lpDesktop", wintypes.LPSTR),
        ("lpTitle", wintypes.LPSTR),
        ("dwX", wintypes.DWORD),
        ("dwY", wintypes.DWORD),
        ("dwXSize", wintypes.DWORD),
        ("dwYSize", wintypes.DWORD),
        ("dwXCountChars", wintypes.DWORD),
        ("dwYCountChars", wintypes.DWORD),
        ("dwFillAttribute", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("wShowWindow", wintypes.WORD),
        ("cbReserved2", wintypes.WORD),
        ("lpReserved2", wintypes.LPBYTE),
        ("hStdInput", wintypes.HANDLE),
        ("hStdOutput", wintypes.HANDLE),
        ("hStdError", wintypes.HANDLE),
    ]

class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("hProcess", wintypes.HANDLE),
        ("hThread", wintypes.HANDLE),
        ("dwProcessId", wintypes.DWORD),
        ("dwThreadId", wintypes.DWORD),
    ]

def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else r"E:\llama\NewChangeDirectory\test\test_ui_exclusions.exe"
    si = STARTUPINFOA()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    ok = kernel32.CreateProcessA(
        exe.encode(),
        None,
        None,
        None,
        False,
        DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
        None,
        None,
        ctypes.byref(si),
        ctypes.byref(pi)
    )
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())
    print(f"Created process {pi.dwProcessId}")
    
    event = DEBUG_EVENT()
    crashed = False
    base_addr = None
    while True:
        if not kernel32.WaitForDebugEvent(ctypes.byref(event), INFINITE):
            break
        code = event.dwDebugEventCode
        tid = event.dwThreadId
        if code == 3:  # CREATE_PROCESS_DEBUG_EVENT
            base_addr = event.u.CreateProcessInfo.lpBaseOfImage
            print(f"Process base = 0x{base_addr:016X}")
            kernel32.CloseHandle(event.u.CreateProcessInfo.hFile)
            kernel32.ContinueDebugEvent(event.dwProcessId, tid, DBG_CONTINUE)
            continue
        if code == 1:  # EXCEPTION_DEBUG_EVENT
            exc = event.u.Exception
            addr = exc.ExceptionRecord.ExceptionAddress
            exc_code = exc.ExceptionRecord.ExceptionCode
            print(f"EXCEPTION in thread {tid}: 0x{exc_code:08X} at 0x{addr:016X} (first chance={exc.dwFirstChance})")
            if exc_code == EXCEPTION_ACCESS_VIOLATION:
                read_write = exc.ExceptionRecord.ExceptionInformation[0]
                fault_addr = exc.ExceptionRecord.ExceptionInformation[1]
                print(f"  Access violation: type={'read' if read_write == 0 else 'write' if read_write == 1 else 'execute'} faulting addr=0x{fault_addr:016X}")
                # Get thread context
                hThread = kernel32.OpenThread(0x001F03FF, False, tid)  # THREAD_ALL_ACCESS
                if hThread:
                    ctx = CONTEXT64()
                    ctx.ContextFlags = CONTEXT_FULL
                    if kernel32.GetThreadContext(hThread, ctypes.byref(ctx)):
                        print(f"  RIP=0x{ctx.Rip:016X} RAX=0x{ctx.Rax:016X}")
                    else:
                        print("  Failed to get thread context")
                    kernel32.CloseHandle(hThread)
                else:
                    print("  Failed to open thread")
                crashed = True
                kernel32.TerminateProcess(pi.hProcess, 1)
                break
            # Continue handling first-chance exceptions
            kernel32.ContinueDebugEvent(event.dwProcessId, tid, DBG_EXCEPTION_NOT_HANDLED)
        elif code == 5:  # EXIT_PROCESS_DEBUG_EVENT
            exit_code = event.u.ExitProcess.dwExitCode
            print(f"Process exited with code {exit_code}")
            break
        else:
            kernel32.ContinueDebugEvent(event.dwProcessId, tid, DBG_CONTINUE)
    
    kernel32.CloseHandle(pi.hProcess)
    kernel32.CloseHandle(pi.hThread)
    if crashed:
        sys.exit(1)

if __name__ == "__main__":
    main()
