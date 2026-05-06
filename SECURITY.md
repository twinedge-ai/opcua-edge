# Security Policy

## Supported Versions

Security fixes are handled on the default branch until tagged releases are
introduced. After release tags exist, supported versions should be documented
in this file.

## Reporting a Vulnerability

Please do not open a public issue for suspected vulnerabilities.

Report security issues through GitHub private vulnerability reporting if it is
enabled for this repository. If private reporting is unavailable, contact the
maintainers through the repository owner's preferred private channel and include:

- Affected commit or release.
- A clear description of the issue and impact.
- Steps to reproduce or a minimal proof of concept.
- Any relevant logs, configuration, or network exposure details.

Maintainers should acknowledge reports within 5 business days and coordinate a
fix and disclosure timeline with the reporter.

## Deployment Notes

`opcua-edge` is intended for controlled industrial networks. Operators should
place the OPC UA and Modbus TCP endpoints behind appropriate network controls,
avoid exposing port 4840 or Modbus port 502/1502 directly to the public
Internet, and protect SQLite database backups as operational data.

The current default OPC UA server configuration uses the minimal open62541
server setup. It is suitable for local development, lab networks, and private
industrial segments, but it does not configure application certificates,
encrypted security policies, or user authentication. Production deployments
should add those controls or terminate access through a trusted gateway before
exposing the service beyond a controlled network.
