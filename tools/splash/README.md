# Startup splash (v1.4)

`mw4splash.dll` shows the embedded, approved artwork once in a process named
`sp26-cod.exe`, only while `trdata2.bin` exists beside the DLL. The window keeps
the image proportions, does not activate itself, and closes on a 5000 ms timer.
No network, hotkeys, launcher changes or game rendering hooks are used.
The worker runs after DLL initialization; DllMain never waits or creates UI.

Build with Zig 0.14.1 and Python with `pefile`:

```
python tools/splash/build.py --zig C:/path/to/zig.exe
```

The build embeds `artwork.bmp` as a bitmap resource and compiles its SHA256 digest
into the DLL. It checks that digest before showing the bitmap. External image
files are ignored. This prevents simple file replacement and detects resource
replacement; it does not prevent a determined party from patching or rebuilding
the DLL, and is not a promise of copy protection.

The build adds an import for `mw4splash.dll!Mw4SplashInit` to the hash-checked v1.3
trhook.dll from Git. All existing section bytes and import descriptors are checked
and preserved, including the rtlcompanion.dll dependency. The approved V15
rtlcompanion.dll remains byte-for-byte unchanged (SHA256
c4ec5e5a46ec2b16447726d64cba2b504ff228880772b52f07c2ea9685335f4a).

Only the runtime DLLs and translation data belong in the consumer ZIP. Source,
build tools, raw translations and standalone artwork remain in the repository.
The ZIP has install.bat, uninstall.bat and installer.ps1 at the top level; no tools
folder is distributed.

The host test verifies the dependency chain, five-second timeout, no foreground
activation, exact embedded artwork bytes, ignored external artwork and rejected
resource tampering. Installation tests use the extracted consumer ZIP. The user
has confirmed the same artwork and five-second timing in the real game. Mock-host
tests alone cannot prove behavior over every fullscreen game or updated build.
