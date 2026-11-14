"""Read present Raspberry Pi USB devices using Windows SetupAPI (no resets)."""
import ctypes as C
from ctypes import wintypes as W


def pico_usb_devices():
    import os
    if os.name != 'nt':
        raise RuntimeError('The batch programmer requires Windows.')
    class GUID(C.Structure):
        _fields_ = [('a', W.DWORD), ('b', W.WORD), ('c', W.WORD), ('d', C.c_ubyte * 8)]
    class INFO(C.Structure):
        _fields_ = [('cbSize', W.DWORD), ('ClassGuid', GUID),
                    ('DevInst', W.DWORD), ('Reserved', C.c_size_t)]
    api = C.WinDLL('setupapi', use_last_error=True)
    api.SetupDiGetClassDevsW.argtypes = [C.c_void_p, W.LPCWSTR, W.HWND, W.DWORD]
    api.SetupDiGetClassDevsW.restype = W.HANDLE
    api.SetupDiEnumDeviceInfo.argtypes = [W.HANDLE, W.DWORD, C.POINTER(INFO)]
    api.SetupDiEnumDeviceInfo.restype = W.BOOL
    api.SetupDiGetDeviceInstanceIdW.argtypes = [W.HANDLE, C.POINTER(INFO), W.LPWSTR,
                                               W.DWORD, C.POINTER(W.DWORD)]
    api.SetupDiGetDeviceInstanceIdW.restype = W.BOOL
    api.SetupDiDestroyDeviceInfoList.argtypes = [W.HANDLE]
    api.SetupDiDestroyDeviceInfoList.restype = W.BOOL
    handle = api.SetupDiGetClassDevsW(None, 'USB', None, 0x02 | 0x04)
    if handle == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    found = set()
    try:
        index = 0
        while True:
            info = INFO(); info.cbSize = C.sizeof(info)
            if not api.SetupDiEnumDeviceInfo(handle, index, C.byref(info)):
                error = C.get_last_error()
                if error == 259:  # ERROR_NO_MORE_ITEMS
                    break
                raise C.WinError(error)
            index += 1
            name = C.create_unicode_buffer(1024)
            if not api.SetupDiGetDeviceInstanceIdW(handle, C.byref(info), name, len(name), None):
                raise C.WinError(C.get_last_error())
            value = name.value.upper()
            # Count physical parents, not each CDC/vendor interface. Includes
            # BOOTSEL so a failed/rebooting board is not treated as unplugged.
            if value.startswith('USB\\VID_2E8A&') and '&MI_' not in value:
                found.add(value)
    finally:
        api.SetupDiDestroyDeviceInfoList(handle)
    return sorted(found)
