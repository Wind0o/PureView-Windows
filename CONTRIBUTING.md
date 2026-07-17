# Contributing

PureView for Windows targets Windows 10/11 x64 and uses only Windows SDK
components. Build with Visual Studio 2022 or newer:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\PureView.exe --self-test
```

Keep the primary interaction contract intact:

- ordinary wheel events browse the current folder;
- `Control` + wheel zooms around the pointer;
- switching images keeps the window top-left position stable;
- the normal window is image-filled and has no bottom status text.

Open a focused pull request and include Windows build/test evidence.
