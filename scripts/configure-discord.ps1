param(
    [Parameter(Mandatory = $true)]
    [string]$DiscordClientId,

    [Parameter(Mandatory = $true)]
    [string]$DiscordClientSecret,

    [string]$AdminDiscordId = "",
    [string]$PublicBaseUrl = "http://127.0.0.1:8080"
)

$ConfigPath = "config/server.json"
$ExamplePath = "config/server.example.json"

if (Test-Path $ConfigPath) {
    $config = Get-Content -Raw -Path $ConfigPath | ConvertFrom-Json
} else {
    $config = Get-Content -Raw -Path $ExamplePath | ConvertFrom-Json
}

$config.public_base_url = $PublicBaseUrl
$config.discord_client_id = $DiscordClientId
$config.discord_client_secret = $DiscordClientSecret
$config.discord_redirect_path = "/auth/discord/callback"
if (-not [string]::IsNullOrWhiteSpace($AdminDiscordId)) {
    $config.admin_discord_id = $AdminDiscordId
}

$config | ConvertTo-Json -Depth 8 | Set-Content -Path $ConfigPath -Encoding UTF8

Write-Host "Discord OAuth configure dans $ConfigPath"
Write-Host "URL de redirection a declarer dans Discord:"
Write-Host "$PublicBaseUrl/auth/discord/callback"
