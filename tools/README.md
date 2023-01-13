# fontconvert

Prerequisite you need to install python3 and install `freetype-py` using pip
The approximate process is like this:

```
sudo apt install python3-pip
python3 -m pip install freetype-py
```

The previous is the prerequisite for implementation, and then you need to store
the font file you want to convert in the same path as fontconvert.py. This is
just for more convenient operation. You can also fill in the font path.

Then just follow the command below to convert the font.

```
python3 fontconvert.py --compress demo 16 msyh.ttc
```

Explanation of specific parameters:

```
python3 fontconvert.py --compress [generated font name] [font size] [font file path]
```

Of course, this only demonstrates the generation of standard ascii codes. If you
need other fonts, you only need to fill in the unicode encoding of the font to
be generated in the `fontconvert.py`  `intervals` list.

Please make sure that the unicode encoding in the fontconvert.py intervals list
is included in your font file, otherwise please comment other encodings and only
keep the 32,126 range!