param(
    [ValidateSet('x64', 'Win32')][string]$Platform = 'x64',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$repositoryForDictionary = Split-Path $PSScriptRoot -Parent
$intermediateForDictionary = Join-Path $PSScriptRoot "intermediate/cmake-build/$Platform/$Configuration"
$dictionaryForBuild = Join-Path $PSScriptRoot 'furigana-dictionary/ipadic-utf8-20070801'
$revisionForDictionary = '61b90ba6e669dc2d7d533d4a80d206f3b31d52b1'
& (Join-Path $repositoryForDictionary '3rdparty/fetch-3rdparty-libs.ps1')
$libraryOutputForDictionary = if ($Platform -eq 'x64') { Join-Path $PSScriptRoot "x64/$Configuration" } else { Join-Path $PSScriptRoot $Configuration }
& $CMake -S $PSScriptRoot -B $intermediateForDictionary -G 'Visual Studio 17 2022' -A $Platform -DCMAKE_SYSTEM_VERSION=10.0.19041.0 "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=`$<1:$libraryOutputForDictionary>"
if ($LASTEXITCODE -ne 0) { throw 'Dictionary compiler configuration failed' }
& $CMake --build $intermediateForDictionary --config $Configuration --target mecab-dict-index --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Dictionary compiler build failed' }
New-Item -ItemType Directory -Force -Path $dictionaryForBuild | Out-Null
& (Join-Path $intermediateForDictionary "mecab/$Configuration/mecab-dict-index.exe") -d (Join-Path $repositoryForDictionary "3rdparty/mecab-$revisionForDictionary/mecab-ipadic") -o $dictionaryForBuild -f EUC-JP -t UTF-8
if ($LASTEXITCODE -ne 0) { throw 'Dictionary compilation failed' }
python (Join-Path $PSScriptRoot 'package_furigana_dictionary.py') $dictionaryForBuild --output (Join-Path $PSScriptRoot 'furigana-dictionary/openlyrics-ipadic-utf8-20070801-v1.zip')
if ($LASTEXITCODE -ne 0) { throw 'Dictionary packaging failed' }
