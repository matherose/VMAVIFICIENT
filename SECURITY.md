# Security Policy

## Reporting a vulnerability

Do NOT open a public GitHub issue for security vulnerabilities.

Send a private disclosure to: joeltordjman@gmail.com  
Include: description, reproduction steps, impact assessment.

Expected response time:

- 48 hours acknowledgement
- 14 days for a fix timeline

## Supported versions

| Version | Supported |
| ------- | --------- |
| main    | Yes       |
| < 1.0   | No        |

## Security measures

This project follows security best practices per AGENTS.md:

- Static analysis with clang-tidy and scan-build (zero findings required)
- Sanitizer builds (ASan, MSan, TSan) in CI/CD
- Memory safety: no unsafe allocations, checked return values
- Input validation: all external inputs validated before use
