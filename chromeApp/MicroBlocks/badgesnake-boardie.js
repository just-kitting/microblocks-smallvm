(function () {
  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
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

  window.BadgeSnakeBoardie = api;
})();
