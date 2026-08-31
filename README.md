# E*TRADE KDE Applet

This repo contains a Plasma 6 applet that shows:

- total portfolio value
- total growth
- daily gain/loss
- your current holdings
- forward-tracked performance charts built from saved daily snapshots and transaction-aware cash-flow sync

## What works today

- OAuth 1.0a sign-in against E*TRADE sandbox or live
- account discovery via the official `accounts/list` endpoint
- holdings + totals via the official `portfolio` endpoint
- local history persistence for real-date `1M / 3M / 6M / 12M / All` chart windows
- forward-only tracking for:
  - account value
  - net invested
  - profit/loss
  - daily P/L with deposits, withdrawals, and transfers removed
  - drawdown from peak
- demo mode for UI testing without API credentials

## Important limitation

E*TRADE's account endpoints do not give this widget a clean multi-month account-value history feed, so the graph is built from **daily snapshots saved locally** by the applet. Net invested and daily P/L are tracked **forward only** from the first saved snapshot for each account/mode by syncing E*TRADE transactions and classifying cash in/out separately from market performance.

## Build

```bash
cmake -S . -B build
cmake --build build
cmake --install build
```

By default the install prefix is `./test`, so the plasmoid package and QML plugin install under that folder for local development.

## Run locally

After installing, point Plasma at the generated package under:

- `test/share/plasma/plasmoids/org.von.etrade`

If you want to install into your user Plasma directories instead, configure CMake with your own prefix.

## Setup flow

1. Create an E*TRADE developer app and get your consumer key/secret.
2. Enter them in the widget.
3. Click **Start E*TRADE Login**.
4. Open the authorization page.
5. Approve access and paste the verifier code back into the widget.
6. Refresh the portfolio.

## Storage

The widget stores its local files under Qt's app data location in an `org.von.etrade` folder:

- `config.json`
- `history.json`

Right now secrets are stored locally in plain JSON for simplicity. A better follow-up would be moving tokens into KWallet.
