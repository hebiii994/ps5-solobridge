# Controlla il payload residente solobridge sulla PS5.
# Uso: .\solobridge.ps1 status | stop | log [-PS5Host 192.168.88.2]
param(
    [ValidateSet("status", "stop")] [string] $Command = "status",
    [string] $PS5Host = "192.168.88.2",
    [int] $Port = 9160
)

$client = New-Object Net.Sockets.TcpClient
try {
    if (-not $client.ConnectAsync($PS5Host, $Port).Wait(3000)) {
        throw "solobridge non risponde su ${PS5Host}:${Port} (non e' in esecuzione?)"
    }
    $stream = $client.GetStream()
    $bytes = [Text.Encoding]::ASCII.GetBytes("$Command`n")
    $stream.Write($bytes, 0, $bytes.Length)
    $reader = New-Object IO.StreamReader($stream)
    Write-Host $reader.ReadToEnd()
} finally {
    $client.Close()
}
