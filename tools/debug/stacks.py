"""Call stacks of every thread of a running process (Windows, x64), symbolised with its PDB: what a hung game waits on.
    python tools/debug/stacks.py <pid | exe name> [max frames] [--vars name,name,...]   (--vars: also the 8-byte values of globals)
Each thread is suspended only while its stack is walked."""
import ctypes, ctypes.wintypes as W, sys

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
dbg = ctypes.WinDLL('dbghelp', use_last_error=True)
k32.OpenProcess.restype = W.HANDLE
k32.OpenThread.restype = W.HANDLE
k32.CreateToolhelp32Snapshot.restype = W.HANDLE

class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ThreadID', W.DWORD), ('th32OwnerProcessID', W.DWORD),
                ('tpBasePri', W.LONG), ('tpDeltaPri', W.LONG), ('dwFlags', W.DWORD)]
class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ProcessID', W.DWORD), ('th32DefaultHeapID', ctypes.c_size_t),
                ('th32ModuleID', W.DWORD), ('cntThreads', W.DWORD), ('th32ParentProcessID', W.DWORD), ('pcPriClassBase', W.LONG),
                ('dwFlags', W.DWORD), ('szExeFile', ctypes.c_char * 260)]

def find_pid(name):
    snap = k32.CreateToolhelp32Snapshot(2, 0)
    e = PROCESSENTRY32(); e.dwSize = ctypes.sizeof(e)
    ok = k32.Process32First(snap, ctypes.byref(e))
    while ok:
        if e.szExeFile.decode(errors='replace').lower() in (name.lower(), name.lower() + '.exe'): return e.th32ProcessID
        ok = k32.Process32Next(snap, ctypes.byref(e))
    return None

def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(4, 0)
    e = THREADENTRY32(); e.dwSize = ctypes.sizeof(e)
    ok = k32.Thread32First(snap, ctypes.byref(e)); out = []
    while ok:
        if e.th32OwnerProcessID == pid: out.append(e.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(e))
    k32.CloseHandle(snap)
    return out

def main():
    args = sys.argv[1:]; names = []
    if '--vars' in args: i = args.index('--vars'); names = args[i + 1].split(','); del args[i:i + 2]
    arg = args[0]; depth = int(args[1]) if len(args) > 1 else 24
    pid = int(arg) if arg.isdigit() else find_pid(arg)
    if not pid: sys.exit('no such process: ' + arg)
    hp = k32.OpenProcess(0x0410 | 0x0008, False, pid)          # QUERY_INFORMATION | VM_READ | VM_OPERATION
    if not hp: sys.exit('OpenProcess failed: %d' % ctypes.get_last_error())
    dbg.SymSetOptions(0x2 | 0x4 | 0x10)                         # UNDNAME | DEFERRED_LOADS | LOAD_LINES
    import os
    k32.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, ctypes.POINTER(W.DWORD)]
    img = ctypes.create_unicode_buffer(1024); n = W.DWORD(1024)
    k32.QueryFullProcessImageNameW(hp, 0, img, ctypes.byref(n))
    dbg.SymInitialize.argtypes = [W.HANDLE, ctypes.c_char_p, W.BOOL]
    if not dbg.SymInitialize(hp, os.path.dirname(img.value).encode(), True): sys.exit('SymInitialize failed: %d' % ctypes.get_last_error())
    dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p; dbg.SymGetModuleBase64.restype = ctypes.c_uint64
    dbg.SymFunctionTableAccess64.argtypes = [W.HANDLE, ctypes.c_uint64]; dbg.SymGetModuleBase64.argtypes = [W.HANDLE, ctypes.c_uint64]
    fta = ctypes.cast(dbg.SymFunctionTableAccess64, ctypes.c_void_p); gmb = ctypes.cast(dbg.SymGetModuleBase64, ctypes.c_void_p)
    dbg.StackWalk64.argtypes = [W.DWORD, W.HANDLE, W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    dbg.SymFromAddr.argtypes = [W.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
    k32.GetThreadDescription.argtypes = [W.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
    sym = ctypes.create_string_buffer(88 + 512)
    def name_of(addr):
        ctypes.memset(sym, 0, len(sym))
        ctypes.c_uint32.from_buffer(sym, 0).value = 88
        ctypes.c_uint32.from_buffer(sym, 0x50).value = 500
        disp = ctypes.c_uint64()
        if dbg.SymFromAddr(hp, addr, ctypes.byref(disp), sym):
            return '%s+0x%x' % (sym.raw[0x54:0x54 + 500].split(b'\0')[0].decode(errors='replace'), disp.value)
        return '0x%x' % addr
    dbg.SymFromName.argtypes = [W.HANDLE, ctypes.c_char_p, ctypes.c_void_p]
    for nm in names:
        ctypes.memset(sym, 0, len(sym)); ctypes.c_uint32.from_buffer(sym, 0).value = 88; ctypes.c_uint32.from_buffer(sym, 0x50).value = 500
        if not dbg.SymFromName(hp, nm.encode(), sym): print('%s: not found' % nm); continue
        addr = ctypes.c_uint64.from_buffer(sym, 0x38).value        # SYMBOL_INFO.Address
        v = ctypes.c_int64(); got = ctypes.c_size_t()
        k32.ReadProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        k32.ReadProcessMemory(hp, ctypes.c_void_p(addr), ctypes.byref(v), 8, ctypes.byref(got))
        print('%s @ 0x%x = %d (0x%x)' % (nm, addr, v.value, v.value & (2**64 - 1)))
    if names and depth == 0: return
    raw = ctypes.create_string_buffer(1232 + 16)
    off = (-ctypes.addressof(raw)) % 16                         # CONTEXT is 16-byte aligned
    ctx = ctypes.addressof(raw) + off
    for tid in threads(pid):
        ht = k32.OpenThread(0x0002 | 0x0008 | 0x0040 | 0x0400, False, tid)
        if not ht: continue
        desc = ctypes.c_wchar_p()
        tname = desc.value if k32.GetThreadDescription(ht, ctypes.byref(desc)) == 0 and desc.value else ''
        k32.SuspendThread(ht)
        try:
            ctypes.memset(ctx, 0, 1232)
            ctypes.c_uint32.from_address(ctx + 0x30).value = 0x10000B   # CONTEXT_FULL (AMD64)
            if not k32.GetThreadContext(ht, ctypes.c_void_p(ctx)): print('thread %d: no context' % tid); continue
            rip, rsp, rbp = (ctypes.c_uint64.from_address(ctx + o).value for o in (0xF8, 0x98, 0xA0))
            sf = ctypes.create_string_buffer(512)
            for o, v in ((0, rip), (32, rbp), (48, rsp)):           # AddrPC, AddrFrame, AddrStack: offset + flat mode
                ctypes.c_uint64.from_buffer(sf, o).value = v; ctypes.c_uint32.from_buffer(sf, o + 12).value = 3
            frames = []
            while len(frames) < depth and dbg.StackWalk64(0x8664, hp, ht, sf, ctypes.c_void_p(ctx), None, fta, gmb, None):
                pc = ctypes.c_uint64.from_buffer(sf, 0).value
                if not pc: break
                frames.append(name_of(pc))
        finally:
            k32.ResumeThread(ht)
        print('--- thread %d %s' % (tid, tname))
        for f in frames: print('    ' + f)
        k32.CloseHandle(ht)

main()
