-- high-level decoder for miniflac, based on coroutines

local miniflac = require'miniflac'

local error = error
local yield = coroutine.yield
local wrap = coroutine.wrap
local insert = table.insert

local function decode_metadata_default()
  return true
end

local function decode_metadata_vorbis_comment(state)
  local vendor_string, comment_string, err, data

  local vorbis_comment = {
    comments = {},
  }

  repeat
    vendor_string, err, state.data = state.decoder:vendor_string(state.data)
    if err then return error('vendor_string: ' .. err) end
    if not vendor_string then
      data = yield(state.blocks)
      state.blocks = {}
      if not data then return false end
      state.data = state.data .. data
    end
  until vendor_string

  vorbis_comment.vendor_string = vendor_string

  while true do
    comment_string, err, state.data = state.decoder:comment_string(state.data)
    if err then return error('comment_string: ' .. err) end
    if not comment_string then --
      if #state.data == 0 then -- we got a MINIFLAC_CONTINUE
        data = yield(state.blocks)
        state.blocks = {}
        if not data then return false end
        state.data = state.data .. data
      else -- if there's data left but no comment, we got MINIFLAC_ITERATOR_END
        break
      end
    else
      insert(vorbis_comment.comments,comment_string)
    end
  end

  state.cur.metadata.vorbis_comment = vorbis_comment
  return true
end

local function decode_metadata_streaminfo(state)
  local streaminfo, err, data

  repeat
    streaminfo, err, state.data = state.decoder:streaminfo(state.data)
    if err then return error('streaminfo: ' .. err) end
    if not streaminfo then
      data = yield(state.blocks)
      state.blocks = {}
      if not data then return false end
      state.data = state.data .. data
    end
  until streaminfo
  state.cur.metadata.streaminfo = streaminfo
  return true
end

local metadata_decoders = {
  unknown        = decode_metadata_default,
  streaminfo     = decode_metadata_streaminfo,
  padding        = decode_metadata_default,
  application    = decode_metadata_default,
  seektable      = decode_metadata_default,
  vorbis_comment = decode_metadata_vorbis_comment,
  cuesheet       = decode_metadata_default,
  picture        = decode_metadata_default,
  invalid        = decode_metadata_default,
}

local function decode_frame(state)
  local frame, err, data
  repeat
    frame, err, state.data = state.decoder:decode(state.data)
    if err then return error('decode: ' .. err) end
    if not frame then
      data = yield(state.blocks)
      state.blocks = {}
      if not data then return false end
      state.data = state.data .. data
    end
  until frame
  state.cur = frame
  return true
end

local function decode_block(state)
  local ok
  if state.cur.type == 'metadata' then
    ok = metadata_decoders[state.cur.metadata.type](state)
  else
    ok = decode_frame(state)
  end
  if ok then
    insert(state.blocks,state.cur)
    state.cur = nil
  end
  return ok
end

local function decoder_create(typ)
  local state = {
    decoder = miniflac.miniflac_t(typ),
    blocks = {},
    cur = nil,
    data = nil,
  }

  return function(data)
    local err
    state.data = data

    while true do
      state.cur, err, state.data = state.decoder:sync(state.data)
      if err then return error('sync: ' .. err) end
      if not state.cur then
        data = yield(state.blocks)
        state.blocks = {}
        if not data then return end
        state.data = state.data .. data
      else
        decode_block(state)
      end
    end
  end
end

local function decoder(typ)
  return wrap(decoder_create(typ))
end

return decoder
