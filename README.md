# libzshim

## Description

libzshim is an LD_PRELOAD hack that acts as an API adapter to make tools
designed to compress with zlib use [zopfli](https://github.com/google/zopfli)
instead.

I wrote it so I could repack deflate streams in PDFs to make them a bit
smaller - I typically get a 4-6% reduction in size.

**⚠️ THIS IS EXPERIMENTAL SOFTWARE ⚠️**

## Getting Started

### Dependencies

* zlib development files
* zopfli development files
* libdl development files
* `make` and `gcc`

### Installation

```
git clone https://github.com/ryancdotorg/libzshim.git
cd libzshim
make
make install
```

You can run `sudo make install` to install system-wide, but probably
shouldn’t.

### Usage

```
LD_PRELOAD=~/.local/lib/libzshim.so \
qpdf --recompress-flate \
     --compression-level=9 \
     --object-streams=generate \
     original.pdf output.pdf
```

### Debugging

Run `make debug` to build libzshim-debug.so version, it will print debug
information to stderr.

## Help

You can file an issue on GitHub, however I may not respond. This software is
being provided without warranty in the hopes that it may be useful.

## Security

All I can say is that libzshim is guaranteed not to backdoor sshd.

## Author

* [Ryan Castellucci](https://rya.nc/) [@ryancdotorg](https://github.com/ryancdotorg) https://rya.nc

## Donations

I am currently [suing the British Government](https://www.leighday.co.uk/news/news/2023-news/legal-challenge-urges-government-to-give-legal-recognition-to-nonbinary-people/).
If you’ve fond my work useful,
**please donate to my [crowdfunding effort](https://enby.org.uk/)**.

## License

This project may be used under the terms of the GNU Affero General Public
License v3.0 or later, a copy of which is provided in
[`LICENSE-AGPL-3.0`](LICENSE-AGPL-3.0).

This means if you want to use it as part of a SaaS offering, you must also
release your code under the same terms. I don’t normally use copyleft licenses,
but I’d be really irritated if someone simply took this and charged people to
use it. Contact me if you’d like to negotiate alternative license terms.
