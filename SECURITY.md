# Security policy

This page tells you how to report a security problem in Hubmote, and
which versions are supported.

## Reporting a security problem

Please report it privately through GitHub, not in a public issue. You
need a GitHub account for this.

1. Open the repository's **Security** tab.
2. Click **Report a vulnerability**. (A vulnerability is a security
   problem.)
3. Say what you found and how to reproduce it. Also say which version of
   the remote's software (the firmware) or of the companion app (the
   Hubmote app on the Google TV) you used.

If the **Report a vulnerability** button isn't there, open an issue that
asks for a private contact. Don't put any details in that issue.

You'll get an answer as soon as possible. This is a hobby project, so
please allow a few weeks for a fix.

## Supported versions

Only the latest release of the firmware and of the companion app is
supported. Older companion apps are a particular risk:

- Versions before 0.6 lack the pairing lockout and the request limits.
  The lockout limits how often someone on your network can guess the
  pairing code. The request limits stop one device from tying up the app.
- Versions 0.3 and older accept commands from any device on your
  network, with no pairing.

## Problems we already know about

What Hubmote protects against, and what it doesn't, is described in
[docs/security.md](docs/security.md). Its
[Known gaps](docs/security.md#known-gaps-and-plans) are known already, so
there's no need to report them. Ideas for closing them are welcome.
