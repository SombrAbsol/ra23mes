<!--
SPDX-FileCopyrightText: 2026 SombrAbsol

SPDX-License-Identifier: MIT
-->

# ra23mes
<a href="https://github.com/SombrAbsol/ra23mes/actions/workflows/ci.yml"><img src="https://github.com/SombrAbsol/ra23mes/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<a href="https://opensource.org/license/mit"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT (Expat)"></a>

MES text file converters for *Pokémon Ranger: Shadows of Almia* and *Pokémon Ranger: Guardian Signs*.

MES files are used to store texts in these two games. *Pokémon Ranger: Shadows of Almia* is the first game to use it, while *Pokémon Ranger: Guardian Signs* use a revised specification of this format, with different text storage and control characters. These tools allow you to convert MES files to JSON and vice versa. You can [download the latest builds](#download) or [build the programs from source](#building).

For more information on the MES format, see [the documentation](/docs/mes.md).

## Download
|        | Linux | macOS | Windows |
| ------ | ----- | ----- | ------- |
| Latest | [ra2mes](https://github.com/SombrAbsol/ra23mes/releases/download/latest/ra2mes-linux.zip)<br>[ra3mes](https://github.com/SombrAbsol/ra23mes/releases/download/latest/ra3mes-linux.zip) | [ra2mes](https://github.com/SombrAbsol/ra23mes/releases/download/latest/ra2mes-macos.zip)<br>[ra3mes](https://github.com/SombrAbsol/ra23mes/releases/download/latest/ra3mes-macos.zip) | [ra2mes](https://github.com/SombrAbsol/ra23mes/releases/download/latest/ra2mes-windows.zip)<br>[ra3mes](https://github.com/SombrAbsol/ra23mes/releases/download/latest/ra3mes-windows.zip) |

## Usage
### Dumping the ROM
You can dump your own *Pokémon Ranger: Guardian Signs* ROM from:
* [a console from the Nintendo DS or Nintendo 3DS family](https://dumping.guide/carts/nintendo/ds) (Game Card release)
* [a Wii U](https://wiki.hacks.guide/wiki/Wii_U:VC_Extract) (Virtual Console release)

### Getting the MES files
To get the MES files, Windows users can run [TinkeDSi](https://github.com/R-YaTian/TinkeDSi), but [NDSFactory](https://github.com/Luca1991/NDSFactory) can also be run on macOS and Linux:
1. Download [the latest NDSFactory release](https://github.com/Luca1991/NDSFactory/releases/latest), then extract the archive and run the executable
2. Open the program, load your ROM, then scroll down until you see the `FAT Files Address` field. Take note or copy its value
3. Press the `Extract Everything` button and choose where to save your files
4. Once the process is complete, go to the `Fat Tools` tab, and fill in the first three fields with the requested files you just extracted (`fat_data.bin`, `fnt.bin` and `fat.bin`) and the fourth with the value from the previous `FAT Files Address` field
5. Press the `Extract FAT Data!` button and choose where to save your files

Go to your output directory. MES files location is different depending of the game:
* *Pokémon Ranger: Shadows of Almia*: in the `data/message` directory
* *Pokémon Ranger: Guardian Signs*: inside the `data_localize` ACF archives in the `data` directory. These are the first 275 (277 in the USA Kiosk Demo) files in the archives, by default with the extension `.bin`. You will need [acftool](https://github.com/SombrAbsol/acftool) to extract them

### Running ra2mes and ra3mes
> [!IMPORTANT]
> ra2mes and ra3mes are command-line programs and must be run in a terminal.
>
> Please use ra2mes for *Pokémon Ranger: Shadows of Almia*'s MES files only, and ra3mes for *Pokémon Ranger: Guardian Signs*' MES files only.

* To convert a MES file to a JSON file, run `ra2mes --to-json in.mes` or `ra3mes --to-json in.mes`
* To convert a JSON file to a MES file, run `ra2mes --to-mes in.json` or `ra3mes --to-mes in.json`

By default, the output file has the same name as the input file, with the extension `.json` or `.mes` depending on the conversion option specified. However, you can optionally specify the output name, for instance:
* `ra2mes --to-json in.mes out.json` will output the JSON file as `out.json`
* `ra3mes --to-mes in.json out.mes` will output the MES file as `out.mes`

The European release of *Pokémon Ranger: Shadows of Almia* uses LZ10-compressed MES files, which have the `.meslz` extension. ra2mes is therefore able to (de)compress these files during conversion:
* `ra2mes --to-json in.meslz` will decompress `in.meslz` and output a JSON file
* `ra2mes --to-meslz in.json` will compress `in.json` and output an LZ10-compressed MES file

## Building
Dependencies: `clang` or `gcc`, and `make`
1. If you don't already have them, install the dependencies
2. Clone this repository by running `git clone https://github.com/SombrAbsol/ra23mes`, or [download the ZIP archive](https://github.com/SombrAbsol/ra23mes/archive/refs/heads/main.zip) and extract it
3. Go to the repository directory and build the programs by running `make`. You can also run `make ra2mes` to only build ra2mes, or `make ra3mes` to only build ra3mes

> [!TIP]
> Running `make` or `make release` will generate release builds. Those available to download are built using this recipe. If you want to generate native or debug builds, run `make native` or `make debug`. Native builds are optimized for your specific CPU for better performance but may not be compatible with other systems, while debug builds include debugging symbols that help diagnose issues but run slower.
>
> Operating systems that use the Unix file system (such as Linux and macOS) can run `sudo make install` to install ra2mes and ra3mes system-wide, preferably after building release or native builds. Use `sudo make uninstall` to remove them.
>
> If you need to rebuild the programs, run `make clean` or delete the `build` directory.

## Credits
ra2mes and ra3mes by [SombrAbsol](https://github.com/SombrAbsol).

## License
ra2mes and ra3mes are free softwares. You can redistribute them and/or modify them under the [terms of the Expat License](/LICENSE) as published by the Massachusetts Institute of Technology.
