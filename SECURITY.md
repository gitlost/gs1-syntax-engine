# Security Policy

## Purpose

The GS1 Barcode Syntax Engine is provided by GS1 AISBL as a permissive open
source library for validating and parsing GS1 barcode data formats such as
element strings and GS1 Digital Link syntax.

This policy sets expectations for how security vulnerabilities are reported,
assessed, remediated, and disclosed.

## Project Scope

This policy applies to the core GS1 Barcode Syntax Engine, including the
vendored BSR Test Routines ("linters"); official language bindings, wrappers,
packages, binaries, WebAssembly builds, the Swift package, and other release
artefacts maintained or distributed by the project; and the project's build and
release processes where a defect or compromise could affect official artefacts.

A vulnerability introduced solely by a third-party wrapper, application,
redistribution, or integration should normally be reported to that project.

The core library has no third-party library dependencies. Official language
bindings and packages have only the minimal dependencies required to execute or
interface with the native code. Vulnerabilities in those dependencies should
normally be reported to their maintainers unless the project's use of the
dependency introduces or materially contributes to the vulnerability.

## Threat Model

The engine processes GS1 barcode data and other input supplied by an integrating
application, along with a Syntax Dictionary file.

Applications may use the engine to process untrusted data, including in
long-running or shared processes.

Inputs are assumed to be supplied through the documented API or through a Syntax
Dictionary file consumed by the engine. Security issues in surrounding
application code or infrastructure are outside scope unless a defect in the
engine directly causes or enables them.

## What Is a Vulnerability

A security vulnerability is a defect that allows input supplied through a
supported interface to have consequences beyond the intended operation of the
library.

Examples include unintended code execution, memory corruption or disclosure,
denial of service through unbounded or grossly disproportionate resource use,
unintended access to host resources, persistent state corruption, or
security-relevant defects in official bindings, packages, build processes, or
release artefacts.

A malformed or malicious Syntax Dictionary may also expose a vulnerability if
processing it causes effects beyond ordinary parsing or validation failure.

## What Is Not a Vulnerability

Correctness, interoperability, consistency, and reduced implementation risk are
important goals of the project. Reports concerning these areas are welcome and
will be taken seriously, but they are not security vulnerabilities unless they
also have a security impact.

Such issues should normally be reported through the issue tracker, including
ordinary correctness, input-handling, resource-use, and development-only issues.

The following are normally the responsibility of the integrating application or
another project:

- Use outside the documented API contract, and sanitisation, escaping, or other
  downstream handling of returned values.
- Decisions about which Syntax Dictionary files or filesystem locations users
  may select, including clearing settings or reinitialising the engine when
  switching between user or trust contexts.
- Security issues caused by how an integrating application receives, transforms,
  stores, logs, or uses data before passing it to the engine or after receiving
  results from it.

## Severity Assessment

Security reports are assessed according to practical impact, exploitability,
affected deployment models and versions, available mitigations, and the
feasibility of remediation.

## Supported Versions and Release Cadence

Development takes place on the master branch.

The project aims to make two scheduled releases each year: one around each major
release of the GS1 General Specifications, and a consolidation release
approximately midway between them. Additional releases may be made when
required, including for security fixes.

The latest release is the primary supported version. We aim to backport Security
fixes to releases made within the previous two years unless backporting is
infeasible. Older releases should not be assumed to receive security fixes
indefinitely.

Users are encouraged to reproduce suspected vulnerabilities against the latest
release or current master branch where practical.

## Reporting a Vulnerability

Suspected security vulnerabilities should be reported privately using GitHub's
private vulnerability reporting.

Select Report a vulnerability on the repository's Security Advisories page,
available through the repository's Security tab.

A useful report should include, where possible, the affected version or build,
relevant platform or binding, a description of the impact, minimal reproduction
steps or proof of concept, and relevant inputs, stack traces, or sanitizer
output.

Please do not include real personal, confidential, or production data.

For non-security bugs, feature requests, and general questions, use the public
issue tracker.

## Response and Remediation

The project aims to acknowledge and provide an initial response to security
reports within one week.

Fix timing depends on severity, complexity, affected components, and the testing
required for a safe release.

For critical vulnerabilities with a straightforward fix, the project aims to
publish a corrected release within one to two weeks of the initial report. More
complex issues may take longer; where practical, mitigations or workarounds may
be provided first.

These are project targets intended to set reasonable expectations rather than
firm deadlines.

## Investigation and Disclosure

Maintainers will assess whether a report is within scope, determine its likely
impact and affected versions where practical, and identify an appropriate
remediation or mitigation.

Reporters are asked to coordinate public disclosure so users and downstream
distributors have a reasonable opportunity to apply a fix or mitigation. Serious
vulnerabilities may remain private until one is available; lower-risk issues may
be handled openly once their security implications are understood.

Reporters may be credited in a published advisory or release information unless
they request anonymity.

## Known Security Issues

None at this time.
