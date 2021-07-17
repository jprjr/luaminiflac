package = "miniflac"
version = "@VERSION@-1"

source = {
  url = "https://github.com/jprjr/luaminiflac/releases/download/v@VERSION@/luaminiflac-@VERSION@.tar.gz"
}

description = {
  summary = "FLAC decoder based in miniflac",
  homepage = "https://github.com/jprjr/luaminiflac",
  license = "MIT"
}

build = {
  type = "builtin",
  modules = {
    ["miniflac"] = {
      sources = {
        "csrc/miniflac.c",
      },
    },
    ["miniflac.decoder"] = "src/miniflac/decoder.lua",
  }
}

dependencies = {
  "lua >= 5.1",
}

