# Security Policy

## Supported Versions

Security updates are applied to the latest commit on the `main` branch.
Users building from source should rebuild after pulling fixes.

## Reporting a Vulnerability

We take the security of BUFUS seriously. If you believe you have found a
security vulnerability, please report it responsibly.

**How to report:**

1. Open a GitHub issue with the label `security`. If you prefer private
   disclosure, you may also email the maintainer at
   security@bufus.example.com with the subject prefix `[SECURITY]`.

2. Include the following in your report:
   - Description of the vulnerability and its potential impact
   - Steps to reproduce or proof-of-concept
   - Affected version(s) or commit range
   - Any known workarounds

**Do not** open public GitHub issues or discuss security vulnerabilities in
public comments, pull requests, or Gists until the issue has been resolved.

## What We Expect From Reporters

- Give us a reasonable amount of time to fix the issue before any
  disclosure to the public or a third party
- Do not exploit the vulnerability or problem you have discovered
- Do not reveal the problem to others until it has been resolved

## What You Can Expect From Us

- Acknowledgement of your report within 7 days
- Status updates on the investigation and fix timeline
- Credit for the reporter (unless you prefer anonymity) upon publication

## Scope of Concern

Given the nature of BUFUS as a low-level disk imaging tool that requires
Administrator privileges, relevant security areas include but are not limited
to:

- **Privilege escalation** — improper elevation or UAC handling
- **Device handle abuse** — unauthorized read/write to unintended drives
- **Buffer overflows** — especially in file/image parsing code
- **Path injection** — directory traversal or symlink attacks on file paths
- **Integer overflows** — in size calculations for disk/partition operations

These are taken seriously even if BUFUS is a small project. All credible
reports will be investigated.

## Disclosure Policy

When a security issue is reported, we will:
1. Confirm the vulnerability and determine its impact
2. Develop a fix and release a new build
3. Publish a GitHub Security Advisory with credit to the reporter
4. Allow a reasonable disclosure window before any public discussion