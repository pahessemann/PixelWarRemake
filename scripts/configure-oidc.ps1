param(
    [Parameter(Mandatory = $true)]
    [string]$ClientId,

    [Parameter(Mandatory = $true)]
    [string]$ClientSecret,

    [string]$AdminSubject = "",
    [string]$ProviderName = "Google",
    [string]$AuthorizationEndpoint = "https://accounts.google.com/o/oauth2/v2/auth",
    [string]$TokenEndpoint = "https://oauth2.googleapis.com/token",
    [string]$UserinfoEndpoint = "https://openidconnect.googleapis.com/v1/userinfo",
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
$config.oidc_provider_name = $ProviderName
$config.oidc_authorization_endpoint = $AuthorizationEndpoint
$config.oidc_token_endpoint = $TokenEndpoint
$config.oidc_userinfo_endpoint = $UserinfoEndpoint
$config.oidc_client_id = $ClientId
$config.oidc_client_secret = $ClientSecret
$config.oidc_redirect_path = "/auth/callback"
if (-not [string]::IsNullOrWhiteSpace($AdminSubject)) {
    $config.admin_oidc_subject = $AdminSubject
}

$config | ConvertTo-Json -Depth 8 | Set-Content -Path $ConfigPath -Encoding UTF8

Write-Host "OIDC configure dans $ConfigPath"
Write-Host "URL de redirection a declarer chez le fournisseur:"
Write-Host "$PublicBaseUrl/auth/callback"
