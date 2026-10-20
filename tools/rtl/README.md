# Subtitle layout helper

Build the x64 DLL with Zig 0.14.1 (or a compatible Windows GNU C compiler):

```
zig cc -target x86_64-windows-gnu -shared -O2 -s rtl_subtitle_signature.c -o rtlcompanion.dll -lkernel32
```

Signatures are scanned only in a private image snapshot read with ReadProcessMemory.
The original executable bytes are not patched. Render callbacks are replaced only
when their native targets and calling mode match. The mode-1 text callback receives
a render context; the context's element pointer is at offset 8.

The tested client UI connection state and key catcher select gameplay versus menus.
Subtitle rows are recognized by their validated hierarchy, alignment, measured text
backgrounds and viewport bounds. Whole name/body subtrees are repositioned together.
Unsupported signatures disable layout initialization. Compatibility with every game
update is not guaranteed. rtlcompanion.log is optional and stored beside the DLL.
