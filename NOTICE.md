# Notice

ADLIB (this package) is an independent reconstruction of the ADLIB system described by Stuart
Rosen and Robert Duisberg in 1996. It is **not affiliated with, authorised or endorsed by
Activision, Wizbang! or the original authors**. HyperBlade, Activision and other names and marks
mentioned in this package belong to their respective owners and are used only to identify the
historical system being described.

**No game content.** This package contains no files, code, data, vocabulary or plans from
HyperBlade. The original executable is identified only by its cryptographic hash, as the source of
the compatibility evidence.

**Methodology.** The language and the compiler's behaviour were determined by analysing the
shipped executable and by running its compiler routine on test inputs under emulation (black-box
comparison). adlibc, the runtime library and the tools are new code written from that analysis
and the resulting specification; see [docs/provenance.md](docs/provenance.md). Short quotations
from Rosen & Duisberg's 1996 *Game Developer* article are used for commentary and identified as
quotations; the article is not reproduced.

The code and documentation in this package are licensed under the MIT licence ([LICENSE](LICENSE)).
