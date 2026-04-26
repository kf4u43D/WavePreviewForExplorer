$ErrorActionPreference = "Stop"

$vcpkgRoot = "C:\dev\vcpkg"
if (!(Test-Path $vcpkgRoot)) {
  Write-Host "Installing vcpkg to $vcpkgRoot"
  git clone https://github.com/microsoft/vcpkg $vcpkgRoot
  & "$vcpkgRoot\bootstrap-vcpkg.bat"
} else {
  Write-Host "vcpkg already exists at $vcpkgRoot"
}

Write-Host "Checking CMake"
cmake --version
Write-Host "Checking Git"
git --version
Write-Host "Bootstrap completed. Configure with ./scripts/configure.ps1"
