# Looks for this PC's personal details in files that will be shipped: user folder paths, the Windows user name
# and the PC name, as plain text and as UTF-16 text. Prints what it finds; exit code 0 = clean, 1 = found something.
# Used by build.bat (the new DLL) and make_release.ps1 (every file going into the zip).
param([Parameter(Mandatory = $true)][string[]]$Files)
$needles = @('\Users\', '/Users/', $env:USERNAME, $env:COMPUTERNAME) | Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique
$latin = [System.Text.Encoding]::GetEncoding(28591)
$found = 0
foreach ($file in $Files) {
  if (-not (Test-Path -LiteralPath $file)) { continue }
  $bytes = [System.IO.File]::ReadAllBytes($file)
  $texts = @($latin.GetString($bytes), [System.Text.Encoding]::Unicode.GetString($bytes))
  if ($bytes.Length -gt 1) { $texts += [System.Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) }
  foreach ($needle in $needles) {
    foreach ($text in $texts) {
      $at = $text.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase)
      while ($at -ge 0) {
        $from = [Math]::Max(0, $at - 40)
        $piece = ($text.Substring($from, [Math]::Min(120, $text.Length - $from)) -replace '[^\x20-\x7E]', '.')
        Write-Host ("  FOUND in {0}: ...{1}..." -f (Split-Path $file -Leaf), $piece)
        $found++
        $at = $text.IndexOf($needle, $at + $needle.Length, [System.StringComparison]::OrdinalIgnoreCase)
      }
    }
  }
}
if ($found -gt 0) { Write-Host (" PERSONAL DETAILS FOUND: {0} place(s) listed above." -f $found); exit 1 }
Write-Host " Personal details check: clean."
exit 0
