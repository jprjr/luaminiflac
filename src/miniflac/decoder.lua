-- high-level decoder for miniflac, based on coroutines

local miniflac = require'miniflac'

local error = error
local yield = coroutine.yield
local wrap = coroutine.wrap
local insert = table.insert
local setmetatable = setmetatable
local match = string.match

local function find_null_char()
  -- this was technically deprecated in lua 5.2 but still works,
  -- we'll test just to be safe
  if match('abc\0','^([^%z]+)%z') == 'abc' then
    return '%z'
  end

  -- if %z didn't match, we must be in some future version
  -- of lua that removed it, try matching on embedded null
  if match('abc\0','^([^\0]+)\0') == 'abc' then
    return '\0'
  end

  -- uh-oh
  return error('unable to determine null character pattern')
end
local null_char = find_null_char()
local null_pattern = '^([^' .. null_char .. ']+)' .. null_char

local function coro_value(f)
  return function(self,len)
    local err, data, result
    repeat
      result, err, self.data = self.decoder[f](self.decoder,self.data,len)
      if err then return error(string.format('%s: %d',f,err)) end
      if not result then
        data = yield(self.blocks)
        self.blocks = {}
        if not data then return nil end
        self.data = self.data .. data
      end
    until result
    return result
  end
end

local Decoder = {}
Decoder.__index = Decoder

for _,k in ipairs(miniflac._metamethods) do
  Decoder[k] = coro_value(k)
end

function Decoder:streaminfo_md5()
  local len = self:streaminfo_md5_length()
  if nil == len then return nil end
  return self:streaminfo_md5_data(len)
end

function Decoder:vorbis_comment_vendor()
  local len = self:vorbis_comment_vendor_length()
  if nil == len then return nil end
  return self:vorbis_comment_vendor_string(len)
end

function Decoder:vorbis_comment()
  local len = self:vorbis_comment_length()
  if nil == len then return nil end
  return self:vorbis_comment_string(len)
end

function Decoder:picture_mime()
  local len = self:picture_mime_length()
  if nil == len then return nil end
  return self:picture_mime_string(len)
end

function Decoder:picture_description()
  local len = self:picture_description_length()
  if nil == len then return nil end
  return self:picture_description_string(len)
end

function Decoder:picture()
  local len = self:picture_length()
  if nil == len then return nil end
  return self:picture_data(len)
end

function Decoder:cuesheet_catalog()
  local len = self:cuesheet_catalog_length()
  if nil == len then return nil end
  return self:cuesheet_catalog_string(len)
end

function Decoder:cuesheet_track_isrc()
  local len = self:cuesheet_track_isrc_length()
  if nil == len then return nil end
  return self:cuesheet_track_isrc_string(len)
end

function Decoder:application()
  local len = self:application_length()
  if nil == len then return nil end
  return self:application_data(len)
end

function Decoder.decode_padding()
  return true
end

function Decoder.decode_invalid()
  return true
end

function Decoder.decode_unknown()
  return true
end

function Decoder:decode_seektable()
  local seektable = {
    seekpoints = {}
  }
  local seekpoints, seekpoint

  seekpoints = self:seektable_seekpoints()
  if nil == seekpoints then return nil end

  for i=1,seekpoints do
    seekpoint = {
      sample_number = nil,
      sample_offset = nil,
      samples = nil,
    }
    seekpoint.sample_number = self:seektable_sample_number()
    if nil == seekpoint.sample_number then return nil end

    seekpoint.sample_offset = self:seektable_sample_offset()
    if nil == seekpoint.sample_offset then return nil end

    seekpoint.samples = self:seektable_samples()
    if nil == seekpoint.samples then return nil end

    seektable.seekpoints[i] = seekpoint
  end

  self.cur.metadata.seektable = seektable
  return true
end

function Decoder:decode_cuesheet()
  local cuesheet = {
    catalog = nil,
    leadin = nil,
    cd_flag = nil,
    tracks = {},
  }
  local tracks, track

  for _,k in ipairs({'catalog','leadin','cd_flag'}) do
    cuesheet[k] = self['cuesheet_' .. k](self)
    if nil == cuesheet[k] then return false end
  end

  cuesheet.catalog = match(cuesheet.catalog,null_pattern)
  cuesheet.cd_flag = cuesheet.cd_flag == 1

  tracks = self:cuesheet_tracks()
  if nil == tracks then return false end

  for i=1,tracks do
    track = {
      offset = nil,
      number = nil,
      isrc = nil,
      audio_flag = nil,
      preemph_flag = nil,
      indexpoints = {},
    }
    local indexpoints, indexpoint

    for _,k in ipairs({'offset','number','isrc','audio_flag','preemph_flag'}) do
      track[k] = self['cuesheet_track_' .. k](self)
      if nil == track[k] then return false end
    end

    track.isrc = match(track.isrc,null_pattern)
    track.audio_flag   = track.audio_flag == 1
    track.preemph_flag = track.preemph_flag == 1

    indexpoints = self:cuesheet_track_indexpoints()
    if nil == indexpoints then return false end

    if indexpoints > 0 then
      for j=1,indexpoints do
        indexpoint = {
          offset = nil,
          number = nil,
        }

        indexpoint.offset = self:cuesheet_index_point_offset()
        if nil == indexpoint.offset then return false end

        indexpoint.number = self:cuesheet_index_point_number()
        if nil == indexpoint.number then return false end

        track.indexpoints[j] = indexpoint
      end
    end

    cuesheet.tracks[i] = track
  end

  self.cur.metadata.cuesheet = cuesheet
  return true
end

function Decoder:decode_application()
  local application = {
    id = nil,
    data = nil,
  }

  application.id = self:application_id()
  if nil == application.id then return false end

  application.data = self:application()
  if nil == application.data then return false end

  self.cur.metadata.application = application
  return true
end

function Decoder:decode_picture()
  local picture = {
    type = nil,
    mime = nil,
    description = nil,
    width = nil,
    height = nil,
    colordepth = nil,
    totalcolors = nil,
    data = nil,
  }

  for _,k in ipairs({
    'type','mime','description','width','height',
    'colordepth','totalcolors'}) do
    picture[k] = self['picture_' .. k](self)
    if nil == picture[k] then return false end
  end

  picture.data = self:picture()
  if nil == picture.data then return false end

  self.cur.metadata.picture = picture
  return true
end

function Decoder:decode_vorbis_comment()
  local vorbis_comment = {
    vendor_string = nil,
    comments = {},
  }

  local comments

  vorbis_comment.vendor_string = self:vorbis_comment_vendor_string()
  if nil == vorbis_comment.vendor_string then return false end

  comments = self:vorbis_comment_total()
  if nil == comments then return false end

  for i=1,comments do
    vorbis_comment.comments[i] = self:vorbis_comment()
    if not vorbis_comment.comments[i] then return false end
  end

  self.cur.metadata.vorbis_comment = vorbis_comment
  return true
end

function Decoder:decode_streaminfo()
  local streaminfo = {
    min_block_size = nil,
    max_block_size = nil,
    min_frame_size = nil,
    max_frame_size = nil,
    sample_rate = nil,
    channels = nil,
    bps = nil,
    total_samples = nil,
    md5 = nil,
  }

  for _,k in ipairs({
    'min_block_size', 'max_block_size', 'min_frame_size', 'max_frame_size',
    'sample_rate', 'channels', 'bps', 'total_samples','md5'}) do
    streaminfo[k] = self['streaminfo_' .. k](self)
    if nil == streaminfo[k] then return false end
  end

  self.cur.metadata.streaminfo = streaminfo
  return true
end

function Decoder:decode_frame()
  local frame = self:decode()
  if not frame then return false end
  self.cur = frame
  return true
end

function Decoder:decode_block()
  local ok
  if self.cur.type == 'metadata' then
    ok = self['decode_' .. self.cur.metadata.type](self)
  else
    ok = self:decode_frame()
  end
  if ok then
    insert(self.blocks,self.cur)
    self.cur = nil
  end
  return ok
end

function Decoder:coro()
  return function(data)
    self.data = data
    while true do
      self.cur = self:sync()
      if not self.cur then return end
      self:decode_block()
    end
  end
end

function Decoder.new(typ)
  local self = setmetatable({
    decoder = miniflac.miniflac_t(typ),
    blocks = {},
    cur = nil,
    data = nil,
  },Decoder)

  return wrap(self:coro())
end

return Decoder
