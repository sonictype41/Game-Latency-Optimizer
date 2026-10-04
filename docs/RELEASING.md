# Releasing GLO OSS

Public releases are fully automated from the `main` branch by `.github/workflows/release.yml`. Maintainers do not create or move release tags by hand.

For each public release:

1. Update `VERSION` / `RELEASE` as appropriate.
2. Add the matching `CHANGELOG.md` entry.
3. Commit and push the release source to `main`.

The release workflow validates the metadata and changelog, checks whether `v<RELEASE>` already exists, rebuilds and tests the Windows client/installer and Linux relay from that exact commit, creates `SHA256SUMS.txt`, creates the matching Git tag at that commit as part of GitHub Release creation, publishes the GitHub Release using the matching changelog entry as its description, marks it Latest, and verifies that the Release is visible before the workflow may succeed.

If a GitHub Release already exists for the current `RELEASE`, the workflow skips publishing. If a tag exists without a matching GitHub Release (for example after an interrupted older automation), it is treated as an orphan: the workflow rebuilds/tests first, then repairs that orphan tag at the current commit immediately before creating the Release. A tag attached to an existing Release is never moved.

Every successfully published public release is marked **Latest**, including alpha/beta/rc builds. The release name and `CHANGELOG.md` entry communicate the release maturity.

The release workflow uses repository-relative build inputs and GitHub-hosted toolchains. It must not depend on a developer username, drive letter, checkout directory, or private service tree.

Do not upload service-only or private artifacts to the public repository or GitHub Release.
