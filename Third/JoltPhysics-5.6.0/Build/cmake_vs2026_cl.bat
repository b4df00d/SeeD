@echo off
rem The "Visual Studio 18 2026" generator needs CMake >= 4.1; prefer the one shipped with VS over any older cmake on PATH.
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "PATH=%%~dpi;%PATH%"
rem SeeD.vcxproj has no <RuntimeLibrary>, so it uses the MSVC default /MD(d); Jolt must match or linking fails.
rem JPH_USE_EXTERNAL_PROFILE routes Jolt's internal profiling into Tracy (SeeD implements
rem ExternalProfileMeasurement in src\Physic.h). Note it REPLACES JPH_PROFILE_ENABLED with
rem JPH_EXTERNAL_PROFILE (Jolt\Jolt.cmake) -- SeeD.vcxproj's define list must match exactly.
rem USE_ASSERTS defines JPH_ENABLE_ASSERTS. Without it JPH_ASSERT compiles out entirely, so Jolt
rem misuse surfaces as a raw crash instead of a named condition + file/line (Physic.h installs a
rem JPH::AssertFailed handler that routes them to IOs::Log). It also changes struct layouts, so
rem SeeD.vcxproj's define list must carry JPH_ENABLE_ASSERTS to match.
cmake -S . -B VS2026_CL -G "Visual Studio 18 2026" -A x64 -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF -DJPH_USE_EXTERNAL_PROFILE=ON -DUSE_ASSERTS=ON %*
echo Open VS2026_CL\JoltPhysics.slnx to build the project.
