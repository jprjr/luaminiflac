# luaminiflac

A FLAC decoder based on [miniflac](https://github.com/jprjr/miniflac).

This is composed of two modules:

## `miniflac`

This is a wrapper around the main `miniflac` library, that tries to follow
the C library's usage very closely.

Generally-speaking, you'll want to allocate a `miniflac_t` object, then
repeatedly call `:sync(data)` until you find a frame boundary, then decode, like:

```lua
local miniflac = require'miniflac'
local decoder = miniflac.miniflac_t()
local result, err, frame, bps

local f = io.open('some-file.flac','rb')
local data = f:read('*a')
f:close()

-- go through blocks
repeat
  result, err, data = decoder:sync(data)
  if err then break end
  if result.type == 'frame' then
    frame, err, data = decoder:decode(data)
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
  elseif result.type == 'metadata' then
    if result.metadata.type == 'streaminfo' then
      -- get something neat from the streaminfo block
      bps, err, data = decoder:streaminfo_bps(data)
      print('bps=' .. bps)
    end
  end
until #data == 0
```


## `miniflac.decoder`

The `miniflac.decoder` module provides a coroutine-based decoder around
the main `miniflac` library, and provides an easy-to-use interface for
decoding FLAC data. You just provide it chunks of data and it returns
an array of blocks that were found.

Here's an example that just reads 4096 bytes at a time

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

The coroutine will automatically put metadata blocks into tables for you.

## LICENSE

BSD Zero Clause (see the `LICENSE` file).

