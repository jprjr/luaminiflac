.PHONY: release clean github-release

PKGCONFIG = pkg-config
CFLAGS = -Wall -Wextra -g -O0
CFLAGS += $(shell $(PKGCONFIG) --cflags lua)

VERSION = $(shell LUA_CPATH="./csrc/?.so" lua -e 'print(require("miniflac")._VERSION)')

lib: csrc/miniflac.so

csrc/miniflac.so: csrc/miniflac.c
	$(CC) -shared $(CFLAGS) -o $@ $^ $(LDFLAGS)

github-release: lib
	source $(HOME)/.github-token && github-release release \
	  --user jprjr \
	  --repo luaminiflac \
	  --tag v$(VERSION)
	source $(HOME)/.github-token && github-release upload \
	  --user jprjr \
	  --repo luaminiflac \
	  --tag v$(VERSION) \
	  --name luaminiflac-$(VERSION).tar.gz \
	  --file dist/luaminiflac-$(VERSION).tar.gz
	source $(HOME)/.github-token && github-release upload \
	  --user jprjr \
	  --repo luaminiflac \
	  --tag v$(VERSION) \
	  --name luaminiflac-$(VERSION).tar.xz \
	  --file dist/luaminiflac-$(VERSION).tar.xz

release: lib
	$(MAKE) clean
	rm -rf dist/luaminiflac-$(VERSION)
	rm -rf dist/luaminiflac-$(VERSION).tar.gz
	rm -rf dist/luaminiflac-$(VERSION).tar.xz
	mkdir -p dist/luaminiflac-$(VERSION)/csrc
	rsync -a csrc/ dist/luaminiflac-$(VERSION)/csrc/
	rsync -a CMakeLists.txt dist/luaminiflac-$(VERSION)/CMakeLists.txt
	rsync -a LICENSE dist/luaminiflac-$(VERSION)/LICENSE
	rsync -a README.md dist/luaminiflac-$(VERSION)/README.md
	sed 's/@VERSION@/$(VERSION)/g' < rockspec/miniflac-template.rockspec > dist/luaminiflac-$(VERSION)/miniflac-$(VERSION)-1.rockspec
	cd dist && tar -c -f luaminiflac-$(VERSION).tar luaminiflac-$(VERSION)
	cd dist && gzip -k luaminiflac-$(VERSION).tar
	cd dist && xz luaminiflac-$(VERSION).tar

clean:
	rm -f csrc/miniflac.so
