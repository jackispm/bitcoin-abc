# CashTab

## Bitcoin Cash Web Wallet

### Features

-   Send & Receive BCH
-   Import existing wallets

## Development

CashTab relies on some modules that retain legacy dependencies. NPM version 7 or later no longer supports automatic resolution of these peer dependencies. To successfully install modules such as `qrcode.react`, with NPM > 7, run `npm install` with the flag `--legacy-peer-deps`

```
npm install --legacy-peer-deps
npm start
```

Runs the app in the development mode.<br>
Open [http://localhost:3000](http://localhost:3000) to view it in the browser.

The page will reload if you make edits.<br>
You will also see any lint errors in the console.

## Testing

### 'npm test'

### 'npm run test:coverage'

## Production

In the project directory, run:

### `npm run build`

Builds the app for production to the `build` folder.<br>
It correctly bundles React in production mode and optimizes the build for the best performance.

The build is minified and the filenames include the hashes.<br>
Your app is ready to be deployed!

See the section about [deployment](https://facebook.github.io/create-react-app/docs/deployment) for more information.

## Browser Extension

1. `npm run extension`
2. Open Chrome or Brave
3. Navigate to `chrome://extensions/` (or `brave://extensions/`)
4. Enable Developer Mode
5. Click "Load unpacked"
6. Select the `extension/dist` folder that was created with `npm run extension`

## Docker deployment

```
npm install --legacy-peer-deps
docker-compose build
docker-compose up
```

## Redundant APIs

CashTab accepts multiple instances of `bch-api` as its backend. Input your desired API URLs separated commas into the `REACT_APP_BCHA_APIS` variable. For example, to run CashTab with three redundant APIs, use:

```
REACT_APP_BCHA_APIS=https://rest.kingbch.com/v3/,https://wallet-service-prod.bitframe.org/v3/,https://free-main.fullstack.cash/v3/
```

You can also run CashTab with a single API, e.g.

```
REACT_APP_BCHA_APIS=https://rest.kingbch.com/v3/
```

CashTab will start with the first API in your list. If it receives an error from that API, it will try the next one.

Navigate to `localhost:8080` to see the app.

## CashTab Roadmap

The following features are under active development:

-   Transaction history
-   Simple Ledger Postage Protocol Support
-   CashTab browser extension
