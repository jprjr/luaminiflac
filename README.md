# luaminiflac

A FLAC decoder based on [miniflac](https://github.com/jprjr/miniflac).

This is composed of two modules:

## `miniflac`

This is a wrapper around the C `miniflac` library, that tries to follow
the C library's usage very closely.

Generally-speaking, you'll want to allocate a `miniflac_t` object, then
repeatedly call `:sync(data)` until you find a frame boundary, then decode.

It requires being pretty familiar with the FLAC spec - in order to decode
metadata blocks, you need to decode each metadata block field in the correct
order, since this doesn't really save/cache information (just like the C library).

```lua
local miniflac = require'miniflac'
local decoder = miniflac.miniflac_t()
local result, err, frame
local streaminfo = {}

local f = io.open('some-file.flac','rb')
local data = f:read('*a')
f:close()

-- go through blocks
repeat
  result, err, data = decoder:sync(data) -- notice the third return value, that's
                                         -- un-processed bytes, save for the next call
  if err then error(err) end
  if result.type == 'frame' then
    frame, err, data = decoder:decode(data)
    if err then error(err) end
    --[[
    do something with the frame, structure is something like:
    { 
      type = "frame",
      frame = {
        header = {
          block_size = 4096,
          blocking_strategy = 0,
          bps = 16,
          channel_assignment = 2,
          channels = 2,
          crc8 = 7,
          frame_number = 124,
          sample_rate = 44100
        },
        samples = { 
          { 1, 2, 3, 4, ... } - channel 1 samples
          { 1, 2, 3, 4, ... } - channel 2 samples,
          ...
        },
      },
    ]]
    print('audio frame, block_size=' .. result.frame.header.block_size)
  elseif result.type == 'metadata' then
    if result.metadata.type == 'streaminfo' then
      -- get the streaminfo block
      streaminfo.min_block_size, err, data = decoder:streaminfo_min_block_size(data)
      if err then error(err) end
      streaminfo.max_block_size, err, data = decoder:streaminfo_max_block_size(data)
      if err then error(err) end
      streaminfo.min_frame_size, err, data = decoder:streaminfo_min_frame_size(data)
      if err then error(err) end
      streaminfo.max_frame_size, err, data = decoder:streaminfo_max_frame_size(data)
      if err then error(err) end
      streaminfo.sample_rate, err, data    = decoder:streaminfo_sample_rate(data)
      if err then error(err) end
      streaminfo.channels, err, data       = decoder:streaminfo_channels(data)
      if err then error(err) end
      streaminfo.bps, err, data            = decoder:streaminfo_bps(data)
      if err then error(err) end
      streaminfo.total_samples, err, data  = decoder:streaminfo_total_samples(data)
      if err then error(err) end
      streaminfo.md5, err, data            = decoder:streaminfo_md5(data,16)
      if err then error(err) end
      print('bps=' .. bps)
    end
  end
until #data == 0
```

## `miniflac.decoder`

The `miniflac.decoder` module provides a coroutine-based decoder around
the main `miniflac` library, and provides an easy-to-use interface for
decoding FLAC data. You just provide it chunks of data and it returns
an array of blocks that were found. You don't have to be as familiar
with the FLAC spec, since it ensures all metadata blocks are handled
in the correct manner.

You can feed the function any number of bytes at a time. If it was
unable to decode any blocks, it just returns an empty array.

Here's an example that decodes a FLAC file in 4096-byte sized
chunks:

```lua
local decoder_lib = require'miniflac.decoder'

local decode = decoder_lib.new()

local f = io.open('some-file.flac','rb')
local data = f:read(4096)

while data do
  local blocks = decode(data)
  for i,b in ipairs(blocks) do
    print(i,b.type)
  end
  data = f:read(4096)
end
decode(nil) -- tell the decoder we're done
f:close()
```

Each returned block is either an audio frame, or a metadata block. Here's
details on the table structure for returned blocks.

### Audio Frames

The audio frame table structure is the same as the one returned
by `miniflac_decode` or `miniflac_t:decode()`:

```lua
{
  type = "frame",
  frame = {
    header = {
      block_size = 4096,
      blocking_strategy = 0,
      bps = 16,
      channel_assignment = 2,
      channels = 2,
      crc8 = 7,
      frame_number = 124, -- will be sample_number if blocking_strategy == 1,
                          -- in which case sample_number is a uint64_t userdata
      sample_rate = 44100
    },
    samples = {
      { 1, 2, 3, 4, ... } -- channel 1 samples,
      { 1, 2, 3, 4, ... } -- channel 2 samples,
      ...
    },
  }
}
```


### Metadata Block: `STREAMINFO`

```lua
{
  type = "metadata",
  metadata = {
    type = "streaminfo",
    is_last = false,
    length = 34, -- the raw length of the original metadata block, in bytes
    streaminfo = {
      min_block_size = 4096,
      max_block_size = 4096,
      min_frame_size = 14,
      max_frame_size = 14066,
      sample_rate = 44100,
      channels = 2,
      bps = 16,
      total_samples = <uint64_t userdata>,
      md5 = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    }
  }
}
```

### Metadata Block: `PADDING`

```lua
{
  type = "metadata",
  metadata = {
    type = "padding",
    is_last = true,
    length = 8192,
    padding = "\0\0\0\0",
  }
}

```

### Metadata Block: `APPLICATION`

```lua
{
  type = "metadata",
  metadata = {
    type = "application",
    is_last = false,
    length = 16,
    application = {
      data = "some-kind-of-data",
      id = 1919510118
    }
  }
}
```

### Metadata Block: `SEEKTABLE`

```lua
{
  type = "metadata",
  metadata = {
    type = "seektable",
    is_last = false,
    length = 612,
    seektable = {
      seekpoints = {
        {
          sample_number = <uint64_t userdata>,
          sample_offset = <uint64_t userndata>,
          samples = 4096
        },
        {
          sample_number = <uint64_t userdata>,
          sample_offset = <uint64_t userdata>,
          samples = 4096
        },
        ...
      }
    }
  }
}
```

### Metadata Block: `VORBIS_COMMENT`

```lua
{
  type = "metadata",
  metadata = {
    type = "vorbis_comment",
    is_last = false,
    length = 40,
    vorbis_comment = {
      vendor_string = "reference libFLAC 1.3.3 20190804",
      comments = {
        "ALBUM=Some Album",
        "ARTIST="Some Artist",
      }
    }
  }
}

```

### Metadata Block: `CUESHEET`

```lua
{
  type = "metadata",
  metadata = {
    type = "cuesheet",
    catalog = "12345678", -- if the catalog number is all zeros in the FLAC, this is nil
    is_last = false,
    length = 2064,
    cuesheet = {
      leadin = <uint64_t userdata>,
      cd_flag = true,
      tracks = {
        {
          offset = <uint64_t userdata>,
          number = 1,
          isrc = "12345678", -- if the ISRC is all zeros in the FLAC, this is nil
          audio_flag = false,
          preemph_flag = false,
          indexpoints = {
            {
              number = 1,
              offset = <uint64_t userdata>
            }
          },
        },
        {
          offset = <uint64_t userdata>,
          number = 2,
          isrc = "12345678", -- if the ISRC is all zeros in the FLAC, this is nil
          audio_flag = false,
          preemph_flag = false
          indexpoints = {
            {
              number = 1,
              offset = <uint64_t userdata>
            }
          }
        }
      }
    }
  }
}
```

### Metadata Block: `PICTURE`

```lua
{
  type = "metadata",
  metadata = {
    type = "picture",
    is_last = false,
    length = 510707,
    picture = {
      type = 3,
      mime = "image/jpeg",
      description = "",
      width = 1416,
      height = 1412,
      colordepth = 24,
      totalcolors = 0,
      data = "binary-picture-data",
    }
  }
}
```

## `uint64_t` userdata

Some FLAC fields require representation larger than 32 bits, in this
case they're represented with a custom userdata. The userdata has a
metatable for addition, subtraction, `tostring`, etc.

```lua
local i = miniflac.uint64_t('9223372036854775806') -- creates a new userdata
i = i + 1
assert(tostring(i) == "9223372036854775807")
```

## LICENSE

BSD Zero Clause (see the `LICENSE` file).

