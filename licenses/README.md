# License and provenance bundle

This directory is part of the Darling Windows port and must remain with any
source or binary distribution.

The port is based on the Darling source tree in `../darling-source`. The
Darling top-level license is GPL-3.0; individual imported components can have
different licenses and copyright holders. Their original license and notice
files remain in their respective source directories and must not be removed or
replaced by this summary.

The Windows host files added by this port are GPL-3.0-only, matching the
Darling integration boundary. The source files carry their own SPDX-style
license statement in their headers.

## Current collected notices

- `LICENSE-darling-GPL-3.0.txt`: copy of the Darling top-level license.
- `darling-debian-copyright`: Debian copyright/provenance metadata from the
  Darling source tree.
- `SOURCE-LICENSE-INVENTORY.csv`: generated inventory of license-like files
  currently present in the checked-out source tree.

This is an inventory, not a relicensing statement. A component with an
unknown or unavailable checkout remains unresolved and must be reviewed before
shipping a binary containing it.

The repository-level license for genuinely Wintosh-original code is
`../LICENSE-WINTOSH-ORIGINAL-MIT.txt`, attributed to Tarek Wasfy. That notice
is intentionally compatible with GPL combination requirements, but it applies
only where the source is genuinely original and explicitly identified as such;
it does not relicense Darling-derived or other third-party code.

For the human-readable cross-project summary, see
`../THIRD-PARTY-NOTICES.md`. Keep this directory, the original source trees,
and the corresponding source available in any redistribution that includes
Darling-derived material.
