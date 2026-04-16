param(
    [string]$PhoneHost = "100.120.2.120",
    [string]$PhoneUser = "u0_a190",
    [int]$PhonePort = 8022
)

$remote = "$PhoneUser@$PhoneHost"

git push origin main
ssh -p $PhonePort $remote "~/run_phone_server.sh"