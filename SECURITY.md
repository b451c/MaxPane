# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 2.3.x (latest) | Yes |
| older 2.x      | Fixes ship in the next release — update via ReaPack |
| 1.x            | No |

## Reporting a Vulnerability

If you discover a security vulnerability in MaxPane, please report it via [GitHub Issues](https://github.com/b451c/MaxPane/issues).

MaxPane is a native C++ REAPER extension that runs locally; its only network access is the optional update check (an HTTPS GET of this repository's ReaPack manifest on GitHub, toggleable in Settings). The attack surface is essentially malformed project files or state data. However, all reports are taken seriously.

### What to expect

- **Acknowledgment** within 48 hours
- **Assessment and fix** within 7 days for confirmed issues
- Credit in the changelog (unless you prefer to remain anonymous)
