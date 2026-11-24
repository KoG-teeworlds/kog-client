# Notice

The KoG client is derived from other projects, in this order:

| Project | Authors | Licence |
| --- | --- | --- |
| Teeworlds | Magnus Auvinen and contributors | zlib |
| DDRace | Shereef Marzouk | zlib |
| [DDNet](https://github.com/ddnet/ddnet) | Dennis Felsing and contributors | zlib; `data/` CC-BY-SA 3.0 except assets, fonts, languages and skins, which have their own licences |
| [TClient](https://github.com/TaterClient/TClient) | Tater and contributors | as DDNet |
| KoG client | King of Gores | zlib |

All upstream notices are kept in [license.txt](license.txt). Under the zlib licence this is an altered version: it is
not the original software and must not be presented as such.

## Names

"KoG Client" is a placeholder name. "Teeworlds", "DDNet" and "TClient" are the names of other projects. This client
is not an official release of any of them and is not endorsed by their authors.

## KoG logos

The icons and the menu logo (`other/icons/KoG-Client*`, `data/tclient/gui_logo.png`) are the official KoG logos from
[brand_and_media](https://github.com/KoG-teeworlds/brand_and_media), scaled and converted but not otherwise changed.
The unmodified originals are in `other/brand/`. These files are **not** under the zlib licence of the code but under
[CC BY-NC-ND 4.0](other/brand/LICENSE); the KoG design team approved their use in this client. A fork that is not KoG
must replace them. The logo does not make this client an official release. See [docs/BRANDING.md](docs/BRANDING.md).

## Added third-party code

| Code | Where | Licence |
| --- | --- | --- |
| Monocypher 4.0.3 | `src/engine/external/monocypher/` | BSD-2-Clause OR CC0-1.0 |

## Assets that need a licence check before the first release

Not verified. Remove or replace what cannot be redistributed.

- `data/tclient/fonts/`: `GoogleSans-Regular.ttf` and `minecraft_font.ttf` have no documented origin or licence in
  the repository. `Cabin`, `Inter`, `Montserrat`, `Nunito` and `Rubik` are usually under the SIL Open Font License
  and `FreeSansBold` under the GPL with a font exception; none of them has a licence file here.
- `docs/assets/TClient_Logo_*.svg` and the other files in `docs/assets/`: TClient's own
  artwork (the README credits its designers); no licence is stated.
- Other files under `data/`: the licence files of DDNet in `data/` cover only what they name.
