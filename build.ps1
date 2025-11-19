# Qt UDP Monitor 编译脚本
# 使用方法: .\build.ps1

Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Cyan
Write-Host "      Qt UDP Monitor 编译脚本" -ForegroundColor Cyan
Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Cyan
Write-Host ""

# 清理旧的build目录
if (Test-Path "build") {
    Write-Host "[1/4] 清理旧的build目录..." -ForegroundColor Yellow
    Remove-Item build -Recurse -Force
}

# 创建build目录
Write-Host "[2/4] 配置CMake项目..." -ForegroundColor Yellow
cmake -S . -B build -G "Ninja" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH="C:/Qt/6.10.0/mingw_64"

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ CMake配置失败!" -ForegroundColor Red
    exit 1
}

# 编译
Write-Host "[3/4] 编译项目..." -ForegroundColor Yellow
cmake --build build --target console_app

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ 编译失败!" -ForegroundColor Red
    exit 1
}

# 检查结果
Write-Host "[4/4] 检查编译结果..." -ForegroundColor Yellow
if (Test-Path "build\console\console_app.exe") {
    $exe = Get-Item "build\console\console_app.exe"
    Write-Host ""
    Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Green
    Write-Host "          ✓✓✓ 编译成功! ✓✓✓" -ForegroundColor Green
    Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Green
    Write-Host ""
    Write-Host "EXE路径: $($exe.FullName)" -ForegroundColor Cyan
    Write-Host "文件大小: $([math]::Round($exe.Length/1MB, 2)) MB" -ForegroundColor Cyan
    Write-Host "更新时间: $($exe.LastWriteTime)" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "运行命令: .\build\console\console_app.exe" -ForegroundColor Yellow
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "✗ 编译失败: 未找到可执行文件" -ForegroundColor Red
    Write-Host ""
    exit 1
}
