param(
    [ValidateSet("--debug", "--release")]
    [string]$BuildType = "--release"
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Write-Log([string]$Msg, [string]$Type = "Info") {
    $Colors = @{ "Info" = "Cyan"; "Success" = "Green"; "Warn" = "Yellow"; "Error" = "Red"; "Link" = "Magenta" }
    $Icons  = @{ "Info" = "[i]"; "Success" = "[+]"; "Warn" = "[!]"; "Error" = "[X]"; "Link" = "[>]" }
    
    $Color = if ($Colors.ContainsKey($Type)) { $Colors[$Type] } else { "White" }
    $Icon  = if ($Icons.ContainsKey($Type)) { $Icons[$Type] } else { "[-]" }

    Write-Host ("{0} {1}" -f $Icon, $Msg) -ForegroundColor $Color
}

$ProjectName = "Aegleseeker Client"
$BuildDir    = "obj"

# Set output directory based on build type
if ($BuildType -eq "--debug") {
    $BinDir = "build/Debug"
} else {
    $BinDir = "build/Release"
}
$Output      = Join-Path $BinDir "aegledll.dll"

# Path to MinGW-w64 compiler
$MinGWPath = "build-tool\mingw64\bin"
$gpp = Join-Path $MinGWPath "g++.exe"
$windres = Join-Path $MinGWPath "windres.exe"

if (!(Test-Path $gpp)) {
    # Try system path
    $sysGpp = Get-Command g++ -ErrorAction SilentlyContinue
    $sysWindres = Get-Command windres -ErrorAction SilentlyContinue
    
    if ($sysGpp -and $sysWindres) {
        $gpp = $sysGpp.Source
        $windres = $sysWindres.Source
        Write-Log "Using system MinGW64 from: $(Split-Path $gpp)" "Info"
    } else {
        # Not found, ask to download
        Write-Log "MinGW64 bins not found at $MinGWPath or system PATH." "Error"
        $choice = Read-Host "Download now? [y/n]"
        if ($choice.ToLower() -ne "y") {
            Write-Log "Build cancelled." "Warn"
            exit
        }
        
        # Download and extract with progress
        Write-Log "Downloading MinGW64 (WinLibs 15.2.0)..." "Info"
        $url = "https://github.com/brechtsanders/winlibs_mingw/releases/download/15.2.0posix-14.0.0-msvcrt-r7/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64msvcrt-14.0.0-r7.zip"
        $zipFile = "mingw_download.zip"
        
        if (!(Test-Path "build-tool")) { New-Item -ItemType Directory -Path "build-tool" | Out-Null }
        
        # Ensure assemblies are loaded for compatibility with Windows PowerShell
        Add-Type -AssemblyName System.Net.Http
        
        try {
            $client = New-Object System.Net.Http.HttpClient
            $response = $client.GetAsync($url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).Result
            $totalSize = $response.Content.Headers.ContentLength
            $readStream = $response.Content.ReadAsStreamAsync().Result
            $fileStream = [System.IO.File]::Create((Join-Path (Get-Location) $zipFile))
            
            $buffer = New-Object byte[] 262144 # 256KB buffer
            $received = 0
            $lastReceived = 0
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            $lastTime = $sw.Elapsed.TotalSeconds
            
            Write-Host ""
            while (($read = $readStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $fileStream.Write($buffer, 0, $read)
                $received += $read
                
                $currTime = $sw.Elapsed.TotalSeconds
                if ($currTime - $lastTime -ge 0.2) {
                    $elapsed = $currTime - $lastTime
                    $speedMbps = (($received - $lastReceived) * 8) / ($elapsed * 1024 * 1024)
                    $mbLeft = ($totalSize - $received) / (1024 * 1024)
                    $percent = ($received / $totalSize) * 100
                    
                    # Dynamic Progress Bar
                    $barWidth = 30
                    $done = [int]($percent * $barWidth / 100)
                    $left = $barWidth - $done
                    $bar = ("#" * $done) + ("-" * $left)
                    
                    $status = "`r    [{0}] {1:N1}% | Speed: {2:N2} Mbps | Remaining: {3:N1} MB" -f $bar, $percent, $speedMbps, $mbLeft
                    Write-Host $status -NoNewline
                    
                    $lastTime = $currTime
                    $lastReceived = $received
                }
            }
            Write-Host "`n"
            $fileStream.Close()
            $readStream.Close()
            $client.Dispose()
        } catch {
            Write-Log "Failed to download using HttpClient. Error: $_" "Error"
            Write-Log "Falling back to basic download..." "Warn"
            Invoke-WebRequest -Uri $url -OutFile $zipFile
        }

        if (Test-Path $zipFile) {
            Write-Log "Extracting (high speed)..." "Info"
            if (Get-Command tar -ErrorAction SilentlyContinue) {
                # tar.exe is extremely fast and built into Win10/11
                tar -xf $zipFile -C "build-tool"
            } else {
                # Fallback to Shell.Application (Explorer engine) which is faster than Expand-Archive
                $shell = New-Object -ComObject Shell.Application
                $zip = $shell.NameSpace((Get-Item $zipFile).FullName)
                $dest = $shell.NameSpace((Get-Item "build-tool").FullName)
                $dest.CopyHere($zip.Items(), 0x10)
            }
            Remove-Item $zipFile
        }
        
        $gpp = Join-Path $MinGWPath "g++.exe"
        $windres = Join-Path $MinGWPath "windres.exe"
        
        if (!(Test-Path $gpp)) {
            Write-Log "Extraction failed or paths are incorrect after download." "Error"
            exit
        }
        Write-Log "MinGW64 installed successfully." "Success"
    }
}

$Sources =  "dllmain.cpp", 
            "ImGui/imgui.cpp", 
            "ImGui/imgui_draw.cpp", 
            "ImGui/imgui_widgets.cpp", 
            "ImGui/imgui_tables.cpp", `
            "ImGui/backend/imgui_impl_dx11.cpp", 
            "ImGui/backend/imgui_impl_win32.cpp", 
            "Modules/Alloc/AllocateNear.cpp",
            "Modules/PatternScan/PatternScan.cpp",
            "Config/ConfigManager.cpp",
            "Modules/Combat/Reach/Reach.cpp",
            "Modules/Combat/Hitbox/Hitbox.cpp",
            "Modules/Movement/AutoSprint/AutoSprint.cpp",
            "Modules/Movement/Timer/Timer.cpp",
            "Modules/Visuals/FullBright/FullBright.cpp",
            "Modules/Visuals/RenderInfo/RenderInfo.cpp",
            "Modules/Visuals/Watermark/Watermark.cpp",
            "Modules/Visuals/MotionBlur/MotionBlur.cpp",
            "Modules/Visuals/Keystrokes/Keystrokes.cpp",
            "Modules/Visuals/CPSCounter/CPSCounter.cpp",
            "Modules/Visuals/FPSOverlay/FPSOverlay.cpp",
            "Modules/Visuals/PingCounter/PingCounter.cpp",
            "Modules/Misc/UnlockFPS/UnlockFPS.cpp",
            "Modules/Terminal/Terminal.cpp",
            "Modules/Info/Info.cpp",
            "Modules/ModuleManager.cpp",
            "ArrayList/ArrayList.cpp",
            "GUI/GUI.cpp",
            "GUI/DX11/ImGuiRenderer.cpp",
            "Hook/Hook.cpp",
            "Input/Input.cpp",
            "Animations/Animations.cpp",
            "Assets/stb/stb_image_impl.cpp",
            "minhook/buffer.c", 
            "minhook/hook.c", 
            "minhook/trampoline.c", 
            "minhook/hde64.c",
            "Networking/Client/IRCClient.cpp",
            "Networking/IRChat.cpp"

$ResourceFile = "Assets/resources.rc"

# Compilation flags based on build type
if ($BuildType -eq "--debug") {
    Write-Log "Building in DEBUG mode..." "Info"
    $Flags = "-g", "-O0", "-fpermissive", "-m64", "-march=x86-64", "-static", "-static-libgcc", "-static-libstdc++", "-I."
} else {
    # dont put -static, breaks the hooks
    Write-Log "Building in RELEASE mode..." "Info"
    $Flags = "-O2", "-s", "-fpermissive", "-m64", "-march=x86-64", "-static", "-static-libgcc", "-static-libstdc++", "-I."
}

$Libs  = "-ld3d11", "-ldxgi", "-ld3dcompiler", "-ldwmapi", "-limm32", "-luser32", "-lgdi32", "-lpsapi", "-lshell32", "-lole32", "-luuid", "-lwinhttp", "-lws2_32"

Clear-Host
Write-Log "Starting Build of $ProjectName (x64)..." "Info"

if (!(Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }
if (!(Test-Path $BinDir))   { New-Item -ItemType Directory -Path $BinDir | Out-Null }

$ObjectFiles = @()
$Total = $Sources.Count
$Count = 0

foreach ($File in $Sources) {
    $Count++
    $CleanPath = $File -replace "^\.\\", ""
    $ObjFile = Join-Path $BuildDir ($CleanPath -replace '\.cpp$|\.c$', '.o')
    
    $ParentDir = Split-Path $ObjFile
    if (!(Test-Path $ParentDir)) { New-Item -ItemType Directory -Path $ParentDir | Out-Null }
    
    $ObjectFiles += $ObjFile

    if (!(Test-Path $ObjFile) -or (Get-Item $File).LastWriteTime -gt (Get-Item $ObjFile).LastWriteTime) {
        $Status = "[{0}/{1}]" -f $Count, $Total
        Write-Log "$Status Compiling: $File" "Warn"
        
        & $gpp -c $File -o $ObjFile $Flags 2>&1 | Tee-Object -Variable Err | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Log "Fatal error in $File." "Error"
            Write-Host ($Err | Out-String) -ForegroundColor Gray
            exit 1
        }
    }
}

# Compile resource file
$ResourceObj = Join-Path $BuildDir "resources.o"
if (!(Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

$AssetsDir = Split-Path $ResourceFile
$AssetsLastWrite = (Get-ChildItem -Path $AssetsDir -Recurse | Measure-Object -Property LastWriteTime -Maximum).Maximum

if (!(Test-Path $ResourceObj) -or $AssetsLastWrite -gt (Get-Item $ResourceObj).LastWriteTime) {
    Write-Log "Compiling resource file: $ResourceFile" "Warn"
    
    & $windres $ResourceFile -O coff -o $ResourceObj 2>&1 | Tee-Object -Variable Err | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Log "Fatal error compiling resources." "Error"
        Write-Host ($Err | Out-String) -ForegroundColor Gray
        exit 1
    }
}

$ObjectFiles += $ResourceObj

Write-Log "Linking binary in directory: $BinDir" "Link"
& $gpp -shared -o $Output $ObjectFiles $Flags $Libs

if ($LASTEXITCODE -eq 0) {
    $Size = [Math]::Round((Get-Item $Output).Length / 1KB, 2)
    $SizeMB = [Math]::Round((Get-Item $Output).Length / 1MB, 2)
    Write-Log "Process completed successfully." "Success"
    Write-Host "----------------------------------" -ForegroundColor Gray
    Write-Host "Generated DLL: $Output"
    Write-Host "File Size:     $Size KB ($SizeMB MB)"
    Write-Host "----------------------------------" -ForegroundColor Gray
    Write-Host "Exit Code: $LASTEXITCODE" -ForegroundColor Gray
} else {
    Write-Log "Error occurred while generating the DLL." "Error"
}
