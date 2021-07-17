package = "miniflac"
version = "dev-1"

source = {
  url = "git+https://github.com/jprjr/luaogg.git"
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

