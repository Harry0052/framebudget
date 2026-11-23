# Security

FrameBudget reads local video and label files and writes a report under the directory you pass as `--output`. It does not authenticate users, and it does not send clips or reports anywhere.

## Reporting a vulnerability

Use GitHub private vulnerability reporting:

https://github.com/Harry0052/framebudget/security

Please include the version or commit, the command you ran, and what you expected to happen. Do not attach private footage. A synthetic clip that reproduces the issue is enough.

## Scope

In scope: file parsing, resource limits, path handling, and crashes or memory errors in this tool.

Out of scope: using the tool as a safety, alarm, or surveillance system. The staged clips are colored proxies. A report is not evidence about a real place or a real emergency.
