# Virtual panel provenance

Panel layout, CSS, logo and the bit-mask lamp update approach are adapted from
Paul Nankervis' PDP-11/70 JavaScript emulator as preserved by James Hagerman:
https://github.com/JamesHagerman/nankervis-pdp11-js
Upstream commit: aca6ba3d633494b0014a75d6b39366c959db1df0.
The upstream SVG is retained unmodified here. The served logo is inline so the
native HTTP server does not need a new MIME type. Attribution is also visible
under the panel. The emulator, disk images and machine-control handlers are not
included: this is a browser-only decorative animation.

The 2.11BSD data pattern follows the idle routine's rdisply=0377, CLC/ROL/BPL/BIS
sequence, including testing the new sign bit (not rotating the old carry).
https://retrocmp.com/index.php?id=75&option=com_content&view=category
The address and status values during bursts are decorative, not live telemetry.
