# IO_BASE


## Getting Started

### Build  for Windows

- debug 

```bash
mkdir win-x64_debug && cd win-x64_debug  
cmake -G  "Visual Studio 17 2022" -T ClangCL -DCMAKE_BUILD_TYPE:STRING="Debug" -DBASE_FETCH_ABSL=ON ../
```

- release

```bash
mkdir win-x64_release && cd win-x64_release    
cmake -G  "Visual Studio 17 2022" -T ClangCL -DCMAKE_BUILD_TYPE:STRING="Release" -DBASE_FETCH_ABSL=ON ../
```

### Build  for Linux

```c
```

