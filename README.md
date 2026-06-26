# Sonic-Loader+ — ⚠️ DISCONTINUED · rebranded to **Elf Arsenal**

> Arabic localization / التعريب العربي: **حيدر أحمد**


> ## ⛔ This project has been renamed and superseded.
>
> Sonic-Loader / Sonic-Loader+ is **no longer maintained under this
> name**. All future development happens under the new identity:
>
> **➡ Elf Arsenal — https://git.etawen.dev/soniciso/elf-arsenal**
>
> Same project, same maintainer, same data dir, expanded feature set
> (PS5 Linux loader baked-in, np-fake-signout, integrated cheat +
> patches engine, version pickers for kstuff/SMP/Linux, Elf Arsenal
> tile/icon, Discord support channel, etc.).
>
> **For existing users — no action required.** The very next in-loader
> update you receive on this `sonic-loader.elf` will be the first Elf
> Arsenal build. It auto-detects the old layout on first boot, renames
> `sonic-loader.elf` → `elf-arsenal.elf` everywhere it finds it (USB,
> ext1, `/data`, `ps5_autoloader/`), patches `autoload.txt` entries
> to match, and your existing `/data/sonic-loader/` config dir is
> reused intact. Next reboot launches as Elf Arsenal.
>
> Support / questions: Discord **https://discord.gg/uPnbsnGAZ**,
> channel **#elf-arsenal**.

The original Sonic-Loader+ README continues below for archival
purposes only — every feature it describes is in Elf Arsenal too.

---

**Manage your jailbroken PS5 from any phone, tablet, or PC on the same Wi-Fi.**

Sonic-Loader+ is one file you send to the PS5. After that, you control the console from a web page — launch any game, manage cheats, install homebrew, transfer files, all without picking up the controller.

If you've never jailbroken a PS5 before, this README walks you through every step.

---

## ⚠️ IMPORTANT — set your PS5's DNS to `127.0.0.1`

> **Preferred DNS: `127.0.0.1`  ·  Secondary DNS: `127.0.0.1`**
>
> On the PS5 go to **Settings → Network → Settings → Set Up Internet Connection → (your network) → Advanced Settings → DNS Settings → Manual** and set **both** DNS servers to `127.0.0.1`.
>
> Sonic-Loader+ runs its own loopback DNS (`nanodns`) inside the PS5 so the console can resolve the hosts the jailbreak chain needs without leaking lookups to your router or your ISP. **Using anything else (auto / your router / 8.8.8.8 / etc.) is the #1 cause of "the jailbreak page won't load" or "Sonic-Loader+ hangs at boot" reports.** Set both Primary and Secondary to `127.0.0.1` and leave them there — every Sonic-Loader+ feature assumes this DNS.

---

## 🆕 1.0.99 — etaHEN is no longer required

**You no longer need etaHEN to jailbreak apps.** This release ships with **Lapy JB Daemon** (`payloads/lapyjb.elf`) — a standalone PID-jailbreak daemon by **voidwhisper** that handles app escalation directly. No etaHEN, no PHU required.

Big thanks to **voidwhisper** for the Lapy JB Daemon — it makes Sonic-Loader+ fully self-contained for app jailbreaking and lets us drop the etaHEN dependency entirely (the `sonic-loader-no-etahen.elf` variant is gone too; there's only one ELF to download now).

Apps that previously needed etaHEN's `HijackerCommand` IPC (Itemzflow, xplorer, anything using the universalps5 PRX) just work — Lapy JB Daemon is on by default and replaces both the bundled `etahen.elf` and the in-loader IPC server (`jb.c`) that used to listen on port 9028.

---

## What it does

A single web UI for everything you'd want to do on a jailbroken PS5:

- 🎮 **Launch any installed game** with a tap (PS4 + PS5 titles).
- 🎯 **Cheats** — turn cheats on and off mid-game; one-click bulk download from etaHEN, GoldHEN, and HEN-Cheats-Collection.
- 📦 **Install homebrew** — RetroArch, ScummVM, EDuke32, and dozens more, one click each.
- 📥 **Install any `.pkg`** — paste a URL, upload from your PC, or pick a file already on the console.
- 🪪 **Offline account activation** — set up user slots without signing in to PSN.
- 🌬️ **Fan control** — set a custom temperature threshold; the console keeps it locked even when games launch.
- 📁 **ShadowMountPlus** auto-mounts games from USB / extended storage. Built-in metadata self-healer fixes blank home-screen tiles automatically.
- 🚀 **Y2JB autoloader sync** keeps your `ps5_autoloader/` folders updated to the latest release for free.
- 💾 **Garlic Worker / SaveMgr** — community save processing, on by default to help everyone's save-decryption queue.
- 🌡️ **Live console temperature** across the top of every page.

…and a Settings panel that exposes every feature with a slider, button, or input. Nothing requires a command line.

---

## Setup (5 minutes)

### What you need

1. **A jailbroken PS5.** Any current jailbreak chain works — Y2JB, BD-JB, Webkit lapse, etc. If your jailbreak normally lets you send `.elf` files to the console (port 9021), you're good.
2. **A computer or phone on the same Wi-Fi as your PS5.** That's the device you'll use as the remote.
3. **The latest `elf-arsenal.elf`** — currently [**1.0.99-app-jb-test**](https://git.etawen.dev/soniciso/sonicloader/releases/tag/v1.0.99-app-jb-test). One flavor only — the etaHEN-bundled variant and `sonic-loader-no-etahen.elf` are both gone now that Lapy JB Daemon (voidwhisper) handles app jailbreaking natively.
   - [`elf-arsenal.elf`](https://git.etawen.dev/soniciso/sonicloader/releases/download/v1.0.99-app-jb-test/elf-arsenal.elf) (~6.4 MB) — bundles Lapy JB Daemon. Launches everything the old etaHEN build did; you don't need etaHEN anymore.

   Optional, only if the home-screen tile doesn't auto-install for you:
   - [`IV9999-PSPS69691_00-SONICLOADER00001.pkg`](https://git.etawen.dev/soniciso/sonicloader/releases/download/v1.0.99-app-jb-test/IV9999-PSPS69691_00-SONICLOADER00001.pkg) (~1.4 MB) — the **Sonic-Loader+** home-screen tile. The loader auto-installs this on boot (Step 3); the direct download is here for manual install via PKG Installer / FTP / etc.

   All releases live on the [Releases page](https://git.etawen.dev/soniciso/sonicloader/releases).

### Step 1 — Send Sonic-Loader+ to the PS5

After your jailbreak chain has loaded ELF Loader on the console (port `9021`), send Sonic-Loader+ over:

**Linux / macOS:**
```sh
nc -q0 <YOUR_PS5_IP> 9021 < elf-arsenal.elf
```

**Windows (PowerShell):**
```powershell
$bytes = [System.IO.File]::ReadAllBytes("elf-arsenal.elf")
$tcp = New-Object System.Net.Sockets.TcpClient("<YOUR_PS5_IP>", 9021)
$tcp.GetStream().Write($bytes, 0, $bytes.Length); $tcp.Close()
```

A notification on the PS5 confirms: *"Sonic-Loader+ serving HTTP on `<your-ps5-ip>:6969`"*.

### Step 2 — Open the web UI

In any browser on the same network:

```
http://<YOUR_PS5_IP>:6969/
```

**Bookmark it.** That's the home screen for everything.

### Step 3 — Wait for the home-screen tile to install (automatic)

About 30 seconds after the loader boots, a **Sonic-Loader+** tile appears on the PS5 home screen automatically. Tapping it opens this web UI directly — no browser bookmarks needed. The PKG is re-fetched on every boot so the tile always points at the latest published version.

Nothing for you to click. Just give it half a minute.

> **It does not overwrite the upstream Homebrew Launcher.** Sonic-Loader+ installs under its own title ID (`PSPS69691`), separate from `ps5-payload-dev/websrv`'s `FAKE00000`. So if you already had the **Homebrew Launcher** tile, you'll end up with **both** icons on the home screen — keep whichever you prefer, or use both. Tap **Homebrew Launcher** for the stock websrv UI, tap **Sonic-Loader+** for this one.

> **Heads up — your PS5's DNS settings matter for this step.** The auto-install fetches the PKG from `git.etawen.dev`, which the PS5's stock Sony DNS may refuse to resolve. As long as your **DNS 1** is one of these, you're fine:
>
> - **`127.0.0.1`** — Sonic-Loader+ bundles a local DNS forwarder (nanoDNS) that resolves community domains automatically. This is the default if you've used Sonic-Loader+'s bundled DNS at all.
> - **`62.210.38.117`** — [Nomadic DNS](https://nomad.gg), a public community DNS that resolves homebrew-related domains. Set this on the PS5 if you don't want to run anything locally.
>
> Set DNS via PS5 → Settings → Network → Set Up Internet Connection → Custom → DNS Settings. Set DNS 1 to one of the two above; DNS 2 can stay as `0.0.0.0`. If neither is configured, the auto-install will silently fail (no error toast) and the tile won't appear; you can still reach the UI at `http://<ps5-ip>:6969/` from a PC/phone browser. Tapping the home-screen tile opens the same page as your PC.

> **Manual install fallback.** If you'd rather not bother with DNS, grab [`IV9999-PSPS69691_00-SONICLOADER00001.pkg`](https://git.etawen.dev/soniciso/sonicloader/releases/download/1.0.81/IV9999-PSPS69691_00-SONICLOADER00001.pkg) directly from the release page and install it via the loader's **Settings → 📦 PKG Installer** (paste path or upload from PC). One-and-done — the tile lives at `PSPS69691` so future loader auto-installs are no-op-with-overwrite over the same content ID.

### Step 4 — First-time kstuff install

The very first time Sonic-Loader+ runs on a console, if you don't already have the kernel-patch helper installed, it auto-downloads the right one for your firmware. You'll see a few PS5 toasts narrate the install. If it doesn't work (no internet, etc.), open Settings → **Install kstuff-lite + ShadowMountPlus** at the top of the panel and pick the button matching your firmware:

- **EchoStretch combo** — for current PS5 firmwares.
- **drakmor combo** — for firmware 10.01 and older.

Click once, wait for the toast, then resend `elf-arsenal.elf` so the loader picks up what just installed.

---

## Daily routine

Once setup is done, every time you want to use the PS5:

1. Wake / boot the console.
2. Run your jailbreak chain to get back into homebrew mode.
3. Send `elf-arsenal.elf` to port `9021` (the same `nc` command from Step 1).
4. Tap the **Sonic-Loader+** home-screen tile, *or* open `http://<ps5-ip>:6969/` in your browser.

**Optional**: the `tools/` folder ships PC-side scripts (`sonic-watchdog.sh` for Linux/macOS, `sonic-watchdog.ps1` for Windows) that watch the PS5 and resend `elf-arsenal.elf` automatically every time the console comes back online. Leave one running on a PC or NAS and Step 3 happens for free.

---

## Tips

- **"System Software Error" on wake from sleep is normal.** Click OK. It's just the Sonic-Loader+ background daemon shutting itself down — the loader doesn't survive rest mode by design. Resend the ELF and you're back.
- **After waking from rest mode**, PS5 games mounted by ShadowMountPlus may refuse to launch from the home screen. Open the web UI → Settings → ShadowMountPlus → **Restart SMP**. One click.
- **Cold boot is slower than usual.** Sonic-Loader+ reads the console's app database and pulls every game's icon the first time. Subsequent visits are instant.
- **Big homebrew installs (RetroArch, EDuke32) take a few minutes** over Wi-Fi. Plug in Ethernet if you can.
- **The bulk cheat-repo download is hundreds of files.** Let it finish in the background; the page shows a progress bar.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Notification says "no kstuff installed yet" | Open Settings, click an Install combo, then resend the loader. |
| Browser page doesn't load | Wait a few seconds and refresh. The previous Sonic-Loader+ instance needs a moment to release the port. |
| Game tile shows just a title ID, no name | Sonic-Loader+ looks the name up online and caches it. Refresh after a moment. |
| Cheat toggle says "no game running" | Cheats only apply to the foreground game. Launch the title first. |
| Fan threshold seems to reset | The PS5 firmware resets fan settings every time a game launches. Sonic-Loader+ re-applies your value within 15 seconds. |
| Home-screen tile doesn't launch the UI | Make sure you're on the current build — old releases pointed the tile at the wrong port. |
| Home-screen tile never appears | The auto-install needs DNS that resolves `git.etawen.dev`. Set the PS5's **DNS 1** to either `127.0.0.1` (Sonic-Loader+'s bundled nanoDNS) or `62.210.38.117` (Nomadic DNS), then resend the loader and wait ~30 seconds. |

If something else breaks, deleting `/data/sonic-loader/.first_boot_done` over FTP and resending the loader takes you back to the first-boot setup flow.

---

## FAQ

**Is it safe?** Sonic-Loader+ runs entirely in user space, the same access level as any other PS5 homebrew. Nothing it does survives a console reboot unless you specifically install it (kstuff, SMP, homebrew, etc.).

**Does it work over the internet?** No. The PS5 and your remote device need to be on the same Wi-Fi network.

**Will it break my games or saves?** No game files are touched. Cheats apply to running game memory only and disappear when the game closes.

**Does it phone home?** No telemetry. Garlic Worker (off-by-default checkbox) connects to garlicsaves.com to help process the community save queue, but it's clearly toggleable and does nothing else.

**Can I use this if I've never jailbroken a PS5?** You need to do the jailbreak chain itself first (Y2JB, BD-JB, Webkit lapse, etc.). Once you can send `.elf` files to the console, Sonic-Loader+ is the easy part.

**What's the build number on the bottom-right corner of every page?** That's the version of the Sonic-Loader+ you're running. Useful when reporting issues — paste that string in the bug report.

---

## Release notes

The current version is [**1.0.81**](https://git.etawen.dev/soniciso/sonicloader/releases/tag/1.0.81). Per-version changelogs live on each [release page](https://git.etawen.dev/soniciso/sonicloader/releases). The version stamp in the bottom-right corner of every web-UI page tells you which build you're running.

---

## Building from source

If you just want to use Sonic-Loader+, grab the prebuilt ELFs from Releases. To build yourself:

```sh
# Need ps5-payload-sdk at /opt/ps5-payload-sdk
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make both
```

That gives you `elf-arsenal.elf` and `sonic-loader-no-etahen.elf`.

To rebuild the home-screen tile PKG (needs Sony's `prospero-pub-cmd` from the leaked PS5 SDK; runs under Wine on Linux):

```sh
cd pkg-source
# Edit values in build_sfo.py if needed, then:
python3 build_sfo.py     # regenerates sce_sys/param.sfo

# Build the .pkg with prospero-pub-cmd (see pkg-source/README.md)
```

Drop the resulting `.pkg` at `payloads/sonic-loader-tile.pkg`, push, and the next loader build serves the new PKG to anyone clicking *Install Sonic-Loader+ UI*.

---

## Credits

Massive thanks to **j0rdy, flat_z, TheFlow, c0w-ar, earthonion, SIStro, egycnq, abkarino, gezine, Dr.Yenyen, zecoxao, StonedModder, VoidWhisper, EchoStretch, BestPig, AlAzif, drakmor, hzhreal, Team-Alua, hammer-83, idlesauce, ntfargo, shahrilnet, null_ptr** — and the entire PS5 jailbreak scene. None of this exists without their work.

Sonic-Loader+ is built on top of:

- [ps5-payload-dev](https://github.com/ps5-payload-dev) — elfldr, websrv, ftpsrv, klogsrv (the foundation)
- [drakmor/shadowMountPlus](https://github.com/drakmor/shadowMountPlus) — game auto-mounting
- [EchoStretch/kstuff-lite](https://github.com/EchoStretch/kstuff-lite) and [drakmor/kstuff-lite](https://github.com/drakmor/kstuff-lite) — kernel patches
- [BestPig/BackPork](https://github.com/BestPig/BackPork) — library sideloading
- [earthonion/garlic-worker](https://git.etawen.dev/earthonion/garlic-worker), [earthonion/garlic-savemgr](https://git.etawen.dev/earthonion/garlic-savemgr) — save processing
- [earthonion/np-fake-signin](https://git.etawen.dev/earthonion/np-fake-signin), [earthonion/np-account-restore](https://git.etawen.dev/earthonion/np-account-restore) — fake-PSN tooling
- [EchoStretch/ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) — app dumper
- [ps5-payload-dev/offact](https://github.com/ps5-payload-dev/offact) — offline account activation
- [GoldHEN](https://github.com/GoldHEN/GoldHEN_Cheat_Repository), [etaHEN](https://github.com/etaHEN/PS5_Cheats), [TeeKay87/HEN-Cheats-Collection](https://github.com/TeeKay87/HEN-Cheats-Collection) — cheat repositories
- [itsPLK/ps5-y2jb-autoloader](https://github.com/itsPLK/ps5-y2jb-autoloader) — autoloader integration

License: **GPLv3+**.
