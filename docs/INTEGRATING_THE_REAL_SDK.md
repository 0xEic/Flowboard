# Integrating the Real OnboardAPI SDK {#integrating_real_sdk}

Flowboard links the real OnboardAPI SDK by default — this guide covers how to
provide it. (If you don't have a license, build with
`-DFLOWBOARD_USE_STUB_SDK=ON` to use the in-tree stub SDK in `sdk_stub/`,
which mimics the public ABI of the
[Rheinmetall OnboardAPI](https://github.com/Rheinmetall/onboardapi) and needs no
proprietary EULA-RME-SDK-1.0 binaries.)

## 1. Obtain the SDK

Download the SDK archive(s) from the OnboardAPI releases page:
<https://github.com/Rheinmetall/onboardapi/releases>. Place them under a
directory of your choice. Choose the right C++ flavour for your platform:

- Windows: `onboardapi_9.9.0_cyclone_Windows_VisualStudio16_MD_x86_64_Release_shared.zip`
- Linux:   `onboardapi_9.9.0_cpp-linux.tar.gz`

## 2. Extract into your build tree

```powershell
Expand-Archive -Path <your-archive>.zip -DestinationPath <project>\sdk
```

This populates `<project>/sdk/cmake/` and `<project>/sdk/include/` with the real
SDK targets.

## 3. Configure CMake

Disable the stub and point at the extracted SDK:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DFLOWBOARD_USE_STUB_SDK=OFF \
      -DCMAKE_PREFIX_PATH=<project>/sdk
```

The engine's `target_link_libraries(... onboardapi::onboardapi)` calls will then
resolve to the real `find_package(onboardapi REQUIRED)` rather than the stub
alias. The codegen pipeline is unchanged — pipegen still reads `.rmodel` files
and emits the same C++ shape; the difference is which `onboardapi::*` library
the generated code links against.

## 4. Set up CycloneDDS

OnboardAPI uses CycloneDDS as its DDS transport. Both the SDK runtime and the
example workflow need it configured:

1. Create a `cyclone.xml` somewhere in your filesystem. The simplest "Localhost"
   profile (single-PC development) is:

   ```xml
   <?xml version="1.0" encoding="UTF-8" ?>
   <CycloneDDS xmlns="https://cdds.io/config" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
     <Domain Id="any">
       <General>
         <Interfaces>
           <NetworkInterface address="127.0.0.1" prefer_multicast="false"/>
         </Interfaces>
       </General>
       <Discovery>
         <ParticipantIndex>auto</ParticipantIndex>
       </Discovery>
     </Domain>
   </CycloneDDS>
   ```

2. Point CycloneDDS at it via environment variable before running
   `flowboard`:

   ```powershell
   # PowerShell (current session)
   $env:CYCLONEDDS_URI = "file:///C:/path/to/cyclone.xml"
   # Verify
   $env:CYCLONEDDS_URI
   ```

   ```bash
   # Linux / macOS
   export CYCLONEDDS_URI=file:///path/to/cyclone.xml
   ```

Other connection types (Wired, Wireless, Unicast-only) and full templates are
covered in the upstream OnboardAPI reference shipped with the SDK; see the
[OnboardAPI releases](https://github.com/Rheinmetall/onboardapi/releases) for
the latest version and documentation.

## 5. Run

```bash
./build/src/flowboard examples/onboard-example-mount-joystick.json
```

With a real Mount service running on the same DDS domain and `serviceName`
matching your `M_Mount.Client` node's config, your pipeline will receive real
`ReportMountPosition` events instead of stub-bus traffic.

## Differences between stub and real

| | Stub SDK | Real SDK (default) |
|---|---|---|
| Transport | In-process function calls | DDS / CycloneDDS UDP |
| Service discovery | None — connect by exact (domain, serviceName) | DDS discovery (multicast or unicast) |
| Threading | Callbacks fire on caller's thread | Callbacks fire on DDS internal thread pool |
| Network configuration | None | `cyclone.xml` required |
| Late joiners | Not modelled | Real DDS durability — late joiners see the last report |
| Cross-host | Not supported | Supported (depending on `cyclone.xml`) |

The generated Service / Client node code is identical between stub and real — only
the linked library changes. This is by design.

## Troubleshooting

- **"No participant found"** at runtime — `cyclone.xml` is missing or
  `CYCLONEDDS_URI` is not set. The OnboardAPI library logs this on startup.
- **Discovery works but no data arrives** — check that the `domainId` in your
  graph JSON matches the publisher's domain. The DDS layer is silent about
  mismatched domains.
- **Build fails: `find_package(onboardapi)` not found** — extract the SDK
  archive correctly; `CMAKE_PREFIX_PATH` must point at the dir containing
  `sdk/cmake/onboardapi-config.cmake`.

## Future direction

Phase 5 plans include:
- A unified `FlowboardProfile` CMake file that bundles the stub vs real
  selection into a single `-DFLOWBOARD_PROFILE=stub|real` switch.
- Optional per-graph DDS QoS overrides exposed in the JSON config.

For now, the toggle is `FLOWBOARD_USE_STUB_SDK`.
