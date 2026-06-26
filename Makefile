# Sonic Loader (App JB Test build) — all-in-one PS5 payload.
#
# Bundles ftpsrv + ShadowMountPlus + a websrv-based web UI that
# can launch any title found in /system_data/priv/mms/app.db.
#
# kstuff is no longer baked in; the user installs it via Settings →
# "Install kstuff-lite + ShadowMountPlus" combo on first boot.
#
# Single build:
#   make             - sonic-loader-app-jb-test.elf
#
# This variant drops the etaHEN payload and the etaHEN-compatible JB
# IPC daemon (jb.c port 9028 + /download0/etahen_jailbreak watcher)
# entirely. Lapy JB Daemon (payloads/lapyjb.elf) is spawned at boot
# instead and handles the same role with no etaHEN dependency.

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

VERSION_TAG := sonic-loader-1.5.7

SONIC_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

PYTHON ?= python3

BIN          := sonic-loader.elf

SRCS := src/main.c src/websrv.c src/asset.c src/fs.c src/mime.c
SRCS += src/mdns.c src/smb.c src/appdb.c src/kmonitor.c src/cheats.c
SRCS += src/activitydb.c
SRCS += src/homebrew.c src/fan.c src/config.c src/avatar.c
SRCS += src/notif_inbox.c src/dashboards.c
SRCS += src/kstuff_updater.c
SRCS += src/smp_updater.c
SRCS += src/smp_meta.c
SRCS += src/y2jb_updater.c
SRCS += src/releases.c
SRCS += src/pkgzone.c
SRCS += src/payload_registry.c
SRCS += src/np.c
SRCS += src/garlic.c
SRCS += src/offact.c
SRCS += src/transfer.c
SRCS += src/jb.c
SRCS += src/dumper.c
SRCS += src/activity.c
SRCS += src/tmdb.c
SRCS += src/ps5/sys.c src/ps5/pt.c src/ps5/elfldr.c src/ps5/hbldr.c
SRCS += src/ps5/notify.c src/ps5/http.c
SRCS += src/third_party/stb_impl.c src/third_party/cJSON.c
SRCS += src/third_party/mc4/aes.c src/third_party/mc4/base64.c
SRCS += src/third_party/mc4/mc4decrypter.c

CFLAGS := -Os -Wall -Werror -Isrc
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -flto
CFLAGS += -DVERSION_TAG=\"$(VERSION_TAG)\"
CFLAGS += -DSONIC_VERSION=\"$(SONIC_VERSION)\"
CFLAGS += -DSONIC_AUTOLAUNCH_HBL
CFLAGS += -DSONIC_NO_ETAHEN

LDFLAGS := -Wl,--gc-sections -flto

LDADD  := -lkernel_sys -lSceSystemService -lSceUserService -lSceAppInstUtil -lScePad
LDADD  += -lSceSsl -lSceHttp -lsqlite3 -lSceRegMgr
LDADD  += `$(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config libmicrohttpd --libs`
LDADD  += `$(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config microdns --libs`
LDADD  += `$(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config libsmb2 --libs`
LDADD  += `$(PS5_PAYLOAD_SDK)/bin/prospero-pkg-config libarchive --libs`

ASSETS   := $(wildcard assets/*)
GEN_SRCS := $(patsubst assets/%,gen/%, $(ASSETS:=.c))

# Sub-payloads bundled in. kstuff.elf is intentionally absent —
# installed at runtime via Settings. etaHEN is intentionally absent
# (this build variant) — Lapy JB Daemon takes its place.
EMBEDDED := payloads/ftpsrv.elf
EMBEDDED += payloads/klogsrv.elf payloads/backpork.elf
EMBEDDED += payloads/np-restore-account.elf
EMBEDDED += payloads/garlic-worker.elf payloads/garlic-savemgr.elf
EMBEDDED += payloads/nanodns.elf
EMBEDDED += payloads/ps5-app-dumper.elf
EMBEDDED += payloads/dpi.elf
EMBEDDED += payloads/smp_icon.png
EMBEDDED += payloads/lapyjb.elf

all: $(BIN)

gen:
	mkdir -p gen

clean:
	rm -rf $(BIN) gen

gen/%.c: assets/% | gen
	$(PYTHON) gen-asset-module.py --path $* $< > $@

$(BIN): $(SRCS) $(GEN_SRCS) $(EMBEDDED)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(GEN_SRCS) $(LDADD)
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip --strip-all $@

deploy: $(BIN)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

.PHONY: all deploy clean
