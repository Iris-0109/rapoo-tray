[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "       雷柏鼠标型号 / 硬件 PID 快速提取工具" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "正在扫描已连接的雷柏 (VID 24AE) 硬件设备..." -ForegroundColor Gray

$knownModels = @{
    "1460" = "雷柏 VT7 系列 (2.4G 无线)"
    "4660" = "雷柏 VT7 系列 (USB 有线)"
    "1406" = "雷柏 VT3S 系列 (2.4G 无线)"
    "1410" = "雷柏 VT3S 系列 (2.4G 无线)"
    "4606" = "雷柏 VT3S 系列 (USB 有线)"
    "1411" = "雷柏 VT3S 系列 (USB 有线)"
    "1412" = "雷柏 VT3 系列 (2.4G 无线)"
    "4612" = "雷柏 VT3 系列 (USB 有线)"
    "1417" = "雷柏 VT3 MAX 系列 (2.4G 无线)"
    "4617" = "雷柏 VT3 MAX 系列 (USB 有线)"
}

try {
    $devs = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*24AE*" }
} catch {
    Write-Host "查询硬件设备失败: $_" -ForegroundColor Red
    exit
}

if (-not $devs) {
    Write-Host ""
    Write-Host "【提示】未检测到任何已连接的雷柏鼠标设备！" -ForegroundColor Yellow
    Write-Host "请确保：1. 鼠标已插上有线线缆或插好 2.4G 接收器；2. 鼠标电源已开启。" -ForegroundColor Gray
    Write-Host ""
    return
}

$foundPids = [System.Collections.Generic.HashSet[string]]::new()
foreach ($d in $devs) {
    if ($d.InstanceId -match 'PID_([0-9A-Fa-f]{4})') {
        [void]$foundPids.Add($Matches[1].ToUpper())
    }
}

if ($foundPids.Count -eq 0) {
    Write-Host "【提示】找到雷柏设备但未能提取到有效的 4 位 PID。" -ForegroundColor Yellow
    return
}

Write-Host "成功检测到 $($foundPids.Count) 个雷柏设备端点 PID：" -ForegroundColor Green
Write-Host ""

$prLines = @()

foreach ($pidCode in $foundPids) {
    $isWired = ($pidCode.StartsWith("46") -or $pidCode -eq "1411")
    $modeStr = if ($isWired) { "USB 有线直连模式" } else { "2.4G 无线接收器模式" }
    
    Write-Host "--------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host "硬件 PID: 0x$pidCode ($modeStr)" -ForegroundColor White
    
    if ($knownModels.ContainsKey($pidCode)) {
        $name = $knownModels[$pidCode]
        Write-Host "收录状态: ✅ 官方已收录专属型号 ($name)" -ForegroundColor Green
    } else {
        Write-Host "收录状态: 🔑 尚未单独打标（当前自动作为通用模式运行，所有功能正常可用）" -ForegroundColor Yellow
        $prSnippet = "    { L`"$pidCode`", L`"雷柏 <请替换为你的具体型号，如 VT9 Pro>`" },"
        $prLines += $prSnippet
        Write-Host "提交建议: 欢迎在 GitHub 提交 PR 将你的设备型号收录至代码库！" -ForegroundColor Cyan
        Write-Host "PR 代码行:" -ForegroundColor Cyan
        Write-Host "  $prSnippet" -ForegroundColor Magenta
    }
}

Write-Host "--------------------------------------------------------" -ForegroundColor DarkGray
Write-Host ""

if ($prLines.Count -gt 0) {
    $clipText = $prLines -join "`r`n"
    try {
        Set-Clipboard -Value $clipText
        Write-Host "【已复制】已自动将 PR 代码片段复制到您的剪贴板！" -ForegroundColor Green
        Write-Host "您可以直接在 GitHub 提交 PR 或在 Issue 中粘贴反馈。" -ForegroundColor Gray
    } catch {
    }
} else {
    Write-Host "您的设备均已被官方数据库收录，无需提交 PR，享受即插即用体验！" -ForegroundColor Green
}
Write-Host ""
