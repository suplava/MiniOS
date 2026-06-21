# Kill old stuff
taskkill /f /im minios.exe 2>$null
taskkill /f /im python.exe 2>$null
Start-Sleep 1

Write-Host "Opening dashboard in browser..." -ForegroundColor Cyan
Start-Process "http://localhost:8765"

Write-Host "Starting MiniOS + Bridge..." -ForegroundColor Green
Write-Host "You will see MiniOS output here. Type commands normally." -ForegroundColor Yellow
Write-Host ""

.\minios.exe 2>&1 | python -u viz\bridge.py --pipe
