# Invia un file .elf al loader ELF della PS5 via TCP.
# Uso: .\send.ps1 hello\hello.elf [-PS5Host 192.168.88.2] [-Port 9021]
param(
    [Parameter(Mandatory)] [string] $Elf,
    [string] $PS5Host = "192.168.88.2",
    [int] $Port = 9021,
    [int] $OutputTimeoutSec = 5
)

$bytes = [IO.File]::ReadAllBytes((Resolve-Path $Elf))
$client = New-Object Net.Sockets.TcpClient
try {
    if (-not $client.ConnectAsync($PS5Host, $Port).Wait(3000)) {
        throw "Nessuna risposta da ${PS5Host}:${Port}. Il loader ELF e' attivo sulla PS5?"
    }
    $stream = $client.GetStream()
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
    $client.Client.Shutdown([Net.Sockets.SocketShutdown]::Send)
    Write-Host ("Inviati {0:N0} byte a {1}:{2}" -f $bytes.Length, $PS5Host, $Port)

    # Mostra l'eventuale output del payload rimandato dal loader
    $stream.ReadTimeout = $OutputTimeoutSec * 1000
    $buf = New-Object byte[] 4096
    try {
        while (($n = $stream.Read($buf, 0, $buf.Length)) -gt 0) {
            Write-Host -NoNewline ([Text.Encoding]::UTF8.GetString($buf, 0, $n))
        }
    } catch [IO.IOException] {
        # timeout senza altro output: normale se il loader non rimanda stdout
    }
} finally {
    $client.Close()
}
