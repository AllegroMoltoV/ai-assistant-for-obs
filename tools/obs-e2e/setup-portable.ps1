# 検証用のポータブル版の OBS を用意する。インストール済みの OBS をコピーし、ビルドしたプラグインと文言を入れ、
# .env の API キーをプラグインと同じ形式で保存し、依頼者のシーンコレクションを写す。配信のキーを含むプロファイルは写さない。
# ポータブル版は ProgramData のプラグインを読まない (OBS 33 の frontend/plugin-manager/PluginModuleLoader_Windows.cpp)
# 引数: 作るフォルダー、.env のパス、リポジトリのフォルダー
param(
	[Parameter(Mandatory = $true)][string]$Root,
	[Parameter(Mandatory = $true)][string]$EnvFile,
	[string]$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path,
	[string]$ObsInstall = "C:\Program Files\obs-studio"
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path "$Root\bin\64bit\obs64.exe")) {
	robocopy $ObsInstall $Root /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "robocopy failed: $LASTEXITCODE" }
}
New-Item -ItemType File -Force "$Root\portable_mode.txt" | Out-Null
Copy-Item "$Repo\build_x64\RelWithDebInfo\ai-assistant-for-obs.dll" "$Root\obs-plugins\64bit\" -Force
$locale = "$Root\data\obs-plugins\ai-assistant-for-obs\locale"
New-Item -ItemType Directory -Force $locale | Out-Null
Copy-Item "$Repo\data\locale\*.ini" $locale -Force

$cfg = "$Root\config\obs-studio"
New-Item -ItemType Directory -Force "$cfg\basic\scenes" | Out-Null
Copy-Item "$env:APPDATA\obs-studio\basic\scenes\scenes.json" "$cfg\basic\scenes\scenes.json" -Force
$ini = if (Test-Path "$cfg\user.ini") { [IO.File]::ReadAllText("$cfg\user.ini") } else { "[General]`n[Basic]`n" }
foreach ($pair in @(@("FirstRun", "false", "General"), @("Language", "ja-JP", "General"),
		@("SceneCollection", "scenes", "Basic"), @("SceneCollectionFile", "scenes.json", "Basic"))) {
	$key, $value, $section = $pair
	if ($ini -match "(?m)^$key=") { $ini = $ini -replace "(?m)^$key=.*$", "$key=$value" }
	else { $ini = $ini -replace "(?m)^\[$section\]\r?$", "[$section]`n$key=$value" }
}
[IO.File]::WriteAllText("$cfg\user.ini", $ini, (New-Object Text.UTF8Encoding $false))

$env:PATH = "$ObsInstall\bin\64bit;$env:PATH"
& "$Repo\build_x64\RelWithDebInfo\aia-model-eval.exe" save-key $EnvFile "$cfg\plugin_config\ai-assistant-for-obs"
if ($LASTEXITCODE -ne 0) { throw "save-key failed" }
"ready: $Root"
