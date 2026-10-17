# Compila un progetto homebrew dentro il container ps5sdk.
# Uso: .\build.ps1 hello            (esegue "make" nella cartella hello)
#      .\build.ps1 hello clean      (argomenti extra passati a make)
param(
    [Parameter(Mandatory)] [string] $Project,
    [Parameter(ValueFromRemainingArguments)] [string[]] $MakeArgs
)

$root = $PSScriptRoot
docker run --rm -v "${root}:/work" -w "/work/$Project" ps5sdk make @MakeArgs
exit $LASTEXITCODE
