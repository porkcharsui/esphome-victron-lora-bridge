# 1Password secret injection

The Nix development shell includes 1Password CLI. The injector reads only the
configured fields, validates them, and atomically updates four Victron and two
Wi-Fi entries in the ignored `.env` file. It does not print fetched values. The
resulting `.env` has mode 0600.

Configure the item IDs and field labels in your ignored `.env` file. The values
in `.env.example` are randomly generated examples and do not identify usable
1Password items or vaults.

| Device | ID setting | MAC field | Key field |
| --- | --- | --- | --- |
| SmartSolar | `OP_SOLAR_ITEM_ID` | `mac` | `encryptionKey` |
| SmartShunt | `OP_SHUNT_ITEM_ID` | `mac` | `encryptionKey` |

The uplink Wi-Fi values come from the item selected by `OP_NETWORK_ITEM_ID` in
the vault selected by `OP_NETWORK_VAULT`:

- `base station password` → `WIFI_PASSWORD`
- `base station name` → `WIFI_SSID`

## Interactive use

Enable the 1Password desktop application's CLI integration, authenticate when
prompted, and run the following from the active project development shell:

```sh
cp .env.example .env
uv run python scripts/inject_1password.py
uv run python scripts/generate_secrets.py
```

The second command validates all credentials before atomically generating
ignored `esphome/secrets.yaml`. Re-run both commands after rotating either
Victron advertisement key.

MAC fields may use lowercase or uppercase hexadecimal with colons, hyphens, or
no separators. The injector normalizes them to ESPHome's uppercase
`XX:XX:XX:XX:XX:XX` form. Other representations remain rejected.

## Service accounts and overrides

For non-interactive use, provide `OP_SERVICE_ACCOUNT_TOKEN` to the command's
environment and set `OP_VAULT` in `.env`; 1Password requires vault scoping for
service-account item reads. Restrict the account to read-only access to that
vault. Do not commit the token or add it to `.env`.

The item and network-vault settings are required. Field labels may be changed
in `.env` if the corresponding 1Password fields use different names:

- `OP_SOLAR_ITEM_ID`
- `OP_SHUNT_ITEM_ID`
- `OP_NETWORK_ITEM_ID`
- `OP_MAC_FIELD`
- `OP_ENCRYPTION_KEY_FIELD`
- `OP_WIFI_PASSWORD_FIELD`
- `OP_WIFI_SSID_FIELD`
- `OP_NETWORK_VAULT`
- `OP_VAULT`

If any item lookup, field lookup, JSON decoding, MAC, key, SSID, or Wi-Fi
password validation fails, `.env` is left untouched.
