# Telegram alerts — setup in plain English

**What you get:** a message on your phone when the car starts a DPF clean-out ("regeneration"), finishes it, or is
interrupted, plus a daily summary with your log files attached. **It is optional.** The logger works fully without it.
On the no-screen board it is switched on in the code but stays quiet until you add a token (it is your only way to
hear about a regeneration); on the touch-screen board you switch it on yourself in step 4.

**What you need:** the free Telegram app on your phone, and the logger able to reach the internet (your home WiFi or
your phone's hotspot). Telegram cannot reach the logger while the car is out of WiFi range; alerts wait and are sent
when it is back online.

You will collect **three things**. They look similar but are different:

| Thing | What it is | Looks like | Secret? |
|---|---|---|---|
| Bot name | what you see in Telegram | `@MyDpfBot` | no |
| Token | the bot's password | `12345678:AAbbCCddEEffGGhh` (a number, a colon, letters) | **YES — treat like a password** |
| Chat id | tells the bot which chat is yours | `123456789` (just digits) | keep private |

## Step 1 — Make your bot (2 minutes)
1. In Telegram, search for **@BotFather** (the official one has a blue tick) and open it.
2. Send `/newbot`. Follow the questions: first a display name, then a username that **ends in `bot`**.
3. BotFather replies with a long line under "Use this token to access the HTTP API". That whole line is your **token**,
   including the number at the start and the colon. Copy it. Do not post it anywhere public.

## Step 2 — Say hello to your bot
Open your new bot (BotFather gave you a link like `t.me/MyDpfBot`), press **Start** and send any message, for example "hi".
The bot cannot find your chat until you have written to it first.

## Step 3 — Find your chat id
1. On a computer or phone browser, open this address, replacing `YOUR-TOKEN` with the token from Step 1
   (keep the word `bot` in front of it, no spaces):

   `https://api.telegram.org/botYOUR-TOKEN/getUpdates`
2. You will see plain text. Find the part that says `"chat"` and, just after it, `"id"`. The number next to it is
   your **chat id**. (If the page shows `"result":[]` nothing is there yet: send another message to your bot and refresh.)
3. Do not screenshot or share that page — it contains your token in the address bar and your messages.

## Step 4 — Put the values in the right file
1. In the project folder, go to `include`. Make a copy of `secrets.example.h` and name the copy `secrets.h`.
   (The copy stays on your computer only; it is never uploaded to GitHub.)
2. Open `secrets.h` in Notepad and fill in the quoted values. Keep the double quotes:
   ```
   #define TG_BOT_TOKEN  "12345678:AAbbCCddEEffGGhh"
   #define TG_CHAT_ID    "123456789"
   ```
   The token goes in as one piece. Do **not** split it at the colon.
3. The logger also needs internet to reach Telegram: fill in your WiFi name and password in the same file
   (`WIFI_STA_SSID` / `WIFI_STA_PASS`). Most home routers are fine; it must be a **2.4 GHz** network.
4. Open `src/config.h` and change `#define ENABLE_TELEGRAM 0` to `#define ENABLE_TELEGRAM 1`.
   If you typed the token wrongly, the build stops with a message telling you what to fix.

## Step 5 — Upload and test
1. Upload the firmware as described in the flashing guide.
2. The board must be joined to a WiFi network that has internet: the network in `WIFI_STA_SSID` at home, or your phone's
   hotspot (`WIFI_STA_SSID2`) in the car. The logger's own `DPF-Sentinel` hotspot has no internet, so it cannot send
   Telegram messages and the `/api/testalert` page does not help while you are only joined to it.
3. As soon as the board joins your network you get a Telegram message: **"Wi-Fi joined: <name>, IP …"**. If it arrives,
   Telegram is working. (Allow up to a minute after power-up.)

## If nothing arrives
| Check | Fix |
|---|---|
| Token typed wrongly | it must contain a colon: `number:letters` |
| Chat id is the bot's number | the chat id comes from the `"chat"` section in Step 3, not from the token |
| You never wrote to the bot | open the bot, press Start, send "hi", then redo Step 3 |
| No internet at the logger | the home WiFi name/password in `secrets.h` must be right and 2.4 GHz |
| In the car, nothing arrives | switch your phone hotspot on (and "Maximise Compatibility" on iPhone); the network must be in `WIFI_STA_SSID2` |
| You only joined the `DPF-Sentinel` hotspot | that network has no internet; the board must join your home or phone network |

## Keeping it safe
- The token lets anyone control your bot. If it ever leaks (a screenshot, a chat, a public post), open @BotFather,
  send `/revoke`, choose your bot, and put the new token into `secrets.h`.
- Never put the token in `config.h` or any file that is uploaded. Only `include/secrets.h` is private.
