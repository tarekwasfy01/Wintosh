# PureDarwin DirectHW reference

The repository is stored in `directhw-source/` at commit
`56d8590b45cc4948b3bb5a73949c8d60b163accd`.

DirectHW is a historical Darwin compatibility layer consisting of a kernel
driver and framework for x86 hardware operations such as `iopl`, port I/O,
MSR access and physical-address mapping. It is relevant only as a reference
for the boundary between user-mode framework calls and privileged kernel
services.

It is not copied into the Darling-for-Windows adapter. Native Windows
implementations of these operations would require a separately signed kernel
driver and security review; the current port intentionally remains a
user-mode compatibility layer and does not expose arbitrary physical memory,
I/O ports or MSRs.

The checkout includes `Copying.rtf`; its component terms must be preserved and
reviewed independently. The repository was archived upstream, so its age and
Darwin/x86 assumptions are not evidence of current Windows compatibility.
