MIDI to Paper Mario BGM Converter
=====
This is a tool for Paper Mario that converts MIDI files into game-ready BGM files for use with Star Rod, etc.

Based on the MIDI tool from the [N64-Tools](https://github.com/jombo23/N64-Tools) repo.

**Note:** This is a CLI tool and must be run from the command line. A GUI version is planned for the future.

Building
--------
### Linux / Mac
```
g++ -o midi2bgm midi2bgm.cpp
```

### Windows (Visual Studio)
```
cl /EHsc midi2bgm.cpp
```

Usage
--------
```
midi2bgm - Converts a MIDI file to a Paper Mario BGM file

Usage: midi2bgm.exe [options] -i <infile> -o <outfile>

Options:
    -i, --infile    <filename>  Input MIDI file
    -o, --outfile   <filename>  Output BGM file

    -l, --loop                  Generate a looping BGM file
    -p, --point     <value>     MIDI loop point value (default: 0)
    -n, --number    <index>     BGM index number
        --name      <name>      BGM index name
    -d, --drum      <track>     Track to mark as percussion (default: none)
```

Contributing
--------
ye

Rad People
--------
* sm64pie - Hella music knowledge, helped a lot with getting this stuff right
* clover - Creator of Star Rod and crazy good at everything PM64-related
* Stryder7x - Gave me a Power Plus badge, so I'm contractually obgligated to put him in here
