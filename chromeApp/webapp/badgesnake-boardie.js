(function () {
  const BRIDGE_NEXT_PATH = '/api/i2c/next';
  const BRIDGE_RESPOND_PATH = '/api/i2c/respond';
  let bridgeStarted = false;

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  async function postJSON(path, body) {
    const response = await fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!response.ok) {
      throw new Error(`request failed: ${response.status}`);
    }
    return response.json();
  }

  async function getJSON(path) {
    const response = await fetch(path, {
      headers: { Accept: 'application/json' },
      cache: 'no-store',
    });
    if (!response.ok) {
      throw new Error(`request failed: ${response.status}`);
    }
    return response.json();
  }

  function normalizeBytes(value) {
    if (!value) return [];
    if (value instanceof Uint8Array) return Array.from(value);
    if (Array.isArray(value)) return value.map((item) => item & 0xff);
    if (typeof value === 'string') {
      const cleaned = value.replace(/0x/g, '').replace(/[^0-9a-f]/gi, '');
      if (cleaned.length % 2 !== 0) throw new Error('hex string must have an even number of nybbles');
      const result = [];
      for (let i = 0; i < cleaned.length; i += 2) {
        result.push(parseInt(cleaned.slice(i, i + 2), 16));
      }
      return result;
    }
    throw new Error('unsupported byte input');
  }

  async function waitForBoardie(timeoutMs) {
    const timeout = timeoutMs || 5000;
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
      if (
        window.GP &&
        GP.boardie &&
        GP.boardie.isOpen &&
        GP.boardie.iframe &&
        GP.boardie.iframe.contentWindow &&
        GP.boardie.iframe.contentWindow.BadgeSnakeI2CTarget
      ) {
        return GP.boardie.iframe.contentWindow;
      }
      await sleep(50);
    }
    throw new Error('Boardie did not become ready');
  }

  const api = {
    open: async function (timeoutMs) {
      if (!(window.GP && GP.boardie && GP.boardie.isOpen)) {
        GP_openBoardie();
      }
      return waitForBoardie(timeoutMs);
    },

    enqueueWrite: async function (address, value) {
      const win = await this.open();
      return win.BadgeSnakeI2CTarget.enqueueWrite(address, normalizeBytes(value));
    },

    enqueueRead: async function (address) {
      const win = await this.open();
      return win.BadgeSnakeI2CTarget.enqueueRead(address);
    },

    takeResponse: async function (address, requestID) {
      const win = await this.open();
      const data = win.BadgeSnakeI2CTarget.takeResponse(address, requestID);
      return data ? Array.from(data) : null;
    },

    transaction: async function (address, value, options) {
      const opts = options || {};
      const timeoutMs = opts.timeoutMs || 5000;
      const pollMs = opts.pollMs || 50;

      if (value && normalizeBytes(value).length) {
        await this.enqueueWrite(address, value);
      }
      const requestID = await this.enqueueRead(address);
      const deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
        const response = await this.takeResponse(address, requestID);
        if (response) return response;
        await sleep(pollMs);
      }
      throw new Error('timed out waiting for Boardie I2C response');
    },
  };

  async function handleBridgeRequest(request) {
    const response = await api.transaction(request.address, request.write || [], {
      timeoutMs: request.timeout_ms || 5000,
      pollMs: 50,
    });
    await postJSON(BRIDGE_RESPOND_PATH, {
      id: request.id,
      response: response,
    });
  }

  async function bridgeLoop() {
    for (;;) {
      try {
        const result = await getJSON(`${BRIDGE_NEXT_PATH}?timeout_ms=1000`);
        if (!result.request) {
          await sleep(100);
          continue;
        }
        await handleBridgeRequest(result.request);
      } catch (error) {
        console.warn('boardie i2c bridge error', error);
        await sleep(500);
      }
    }
  }

  function startBridge() {
    if (bridgeStarted) return;
    bridgeStarted = true;
    bridgeLoop();
  }

  api.startBridge = startBridge;

  window.BadgeSnakeBoardie = api;
  window.BoardieI2C = api;
  startBridge();
})();
