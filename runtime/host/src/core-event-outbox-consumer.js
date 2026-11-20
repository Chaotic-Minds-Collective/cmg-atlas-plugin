const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const SERVICE_CREDENTIAL_PATTERN = /^cg_srv_[A-Za-z0-9_-]{40,}$/;
const DEFAULT_REQUEST_TIMEOUT_MS = 10_000;

export class CoreEventOutboxError extends Error {
  constructor(message, options = {}) {
    super(message, options);
    this.name = "CoreEventOutboxError";
    this.status = options.status ?? null;
  }
}

function requireUuid(value, field) {
  if (typeof value !== "string" || !UUID_PATTERN.test(value)) {
    throw new TypeError(`${field} must be a UUID`);
  }
  return value.toLowerCase();
}

function normalizeBaseUrl(value) {
  let url;
  try {
    url = new URL(value);
  } catch (error) {
    throw new TypeError("baseUrl must be an absolute URL", { cause: error });
  }
  const localHosts = new Set(["localhost", "127.0.0.1", "[::1]"]);
  if (url.protocol !== "https:" && !(url.protocol === "http:" && localHosts.has(url.hostname))) {
    throw new TypeError("baseUrl must use HTTPS (HTTP is allowed only for localhost tests)");
  }
  if (url.username || url.password || url.search || url.hash) {
    throw new TypeError("baseUrl must not contain credentials, query parameters, or a fragment");
  }
  url.pathname = url.pathname.replace(/\/+$/, "");
  return url;
}

async function cancelResponseBody(response) {
  try {
    await response?.body?.cancel();
  } catch {
    // Best-effort cleanup; preserve the HTTP result.
  }
}

function requireClaimedEvent(row, serverId) {
  if (!row || typeof row !== "object" || Array.isArray(row)) {
    throw new CoreEventOutboxError("Core returned an invalid outbox record");
  }
  const id = requireUuid(row.id, "outbox event id");
  const leaseToken = requireUuid(row.leaseToken, "leaseToken");
  if (row.eventType !== "game.event.received") {
    throw new CoreEventOutboxError("Core returned an unsupported outbox record type");
  }
  if (!row.payload || typeof row.payload !== "object" || Array.isArray(row.payload)) {
    throw new CoreEventOutboxError("Core returned an invalid game event payload");
  }
  if (typeof row.payload.serverId !== "string" || row.payload.serverId.toLowerCase() !== serverId) {
    throw new CoreEventOutboxError("Core returned an event outside this server scope");
  }
  if (typeof row.payload.eventType !== "string" || row.payload.eventType.trim() === "") {
    throw new CoreEventOutboxError("Core returned an event without a normalized event type");
  }
  if (!Number.isInteger(row.attempts) || row.attempts < 1) {
    throw new CoreEventOutboxError("Core returned an invalid delivery attempt count");
  }
  if (typeof row.leaseExpiresAt !== "string" || !Number.isFinite(Date.parse(row.leaseExpiresAt))) {
    throw new CoreEventOutboxError("Core returned an invalid lease expiration");
  }
  return Object.freeze({
    id,
    eventType: row.eventType,
    aggregateType: typeof row.aggregateType === "string" ? row.aggregateType : null,
    aggregateId: typeof row.aggregateId === "string" ? row.aggregateId : null,
    payload: Object.freeze({ ...row.payload }),
    occurredAt: row.occurredAt,
    attempts: row.attempts,
    leaseToken,
    leaseExpiresAt: row.leaseExpiresAt
  });
}

function safeErrorMessage(error, credential) {
  const raw = error instanceof Error ? error.message : "Event handler failed";
  const safe = raw.replaceAll(credential, "[redacted]").replace(/[\u0000-\u001f\u007f]/g, " ").trim();
  return (safe || "Event handler failed").slice(0, 1000);
}

/**
 * Create a host-owned consumer for Core's server-scoped event outbox.
 * Delivery is sequential and at-least-once: handlers must be idempotent.
 * Plugin callbacks receive event data only; Core credentials remain in this host boundary.
 */
export function createCoreEventOutboxConsumer(options = {}) {
  const baseUrl = normalizeBaseUrl(options.baseUrl);
  const credential = options.credential;
  if (typeof credential !== "string" || !SERVICE_CREDENTIAL_PATTERN.test(credential)) {
    throw new TypeError("credential must be a valid Core service credential");
  }
  const serverId = requireUuid(options.serverId, "serverId");
  const processEvent = options.processEvent;
  if (typeof processEvent !== "function") {
    throw new TypeError("processEvent must be a function");
  }
  const fetchImpl = options.fetchImpl ?? globalThis.fetch;
  if (typeof fetchImpl !== "function") throw new TypeError("fetchImpl must be a function");

  const limit = options.limit ?? 1;
  const leaseSeconds = options.leaseSeconds ?? 120;
  const requestTimeoutMs = options.requestTimeoutMs ?? DEFAULT_REQUEST_TIMEOUT_MS;
  const eventTypes = options.eventTypes;
  if (limit !== 1) throw new TypeError("limit must be 1 until the host can renew every outstanding event lease concurrently");
  if (!Number.isInteger(leaseSeconds) || leaseSeconds < 15 || leaseSeconds > 300) throw new TypeError("leaseSeconds must be an integer between 15 and 300");
  if (!Number.isInteger(requestTimeoutMs) || requestTimeoutMs < 1 || requestTimeoutMs > 60_000) throw new TypeError("requestTimeoutMs must be an integer between 1 and 60000");
  if (eventTypes !== undefined && (!Array.isArray(eventTypes) || eventTypes.length < 1 || eventTypes.length > 50 || eventTypes.some((type) => typeof type !== "string" || type.trim() === "" || type.length > 128) || new Set(eventTypes).size !== eventTypes.length)) {
    throw new TypeError("eventTypes must contain 1 to 50 unique non-empty event type strings");
  }

  const basePath = baseUrl.pathname === "/" ? "" : baseUrl.pathname.replace(/\/+$/, "");
  const endpoint = `${baseUrl.origin}${basePath}/integrations/servers/${serverId}/events`;
  let polling = false;

  async function request(path, body) {
    const controller = new AbortController();
    let timedOut = false;
    const timeout = setTimeout(() => {
      timedOut = true;
      controller.abort(new Error("Core outbox request timed out"));
    }, requestTimeoutMs);
    let response;
    try {
      response = await fetchImpl(`${endpoint}${path}`, {
        method: "POST",
        redirect: "error",
        signal: controller.signal,
        headers: {
          authorization: `Bearer ${credential}`,
          "content-type": "application/json",
          accept: "application/json"
        },
        body: JSON.stringify(body)
      });
      if (!response.ok) {
        const status = response.status;
        await cancelResponseBody(response);
        throw new CoreEventOutboxError(`Core outbox request failed with HTTP ${status}`, { status });
      }
      const text = await response.text();
      let result;
      try {
        result = JSON.parse(text);
      } catch (error) {
        throw new CoreEventOutboxError("Core returned an invalid JSON response", { cause: error, status: response.status });
      }
      return { status: response.status, result };
    } catch (error) {
      if (timedOut) throw new CoreEventOutboxError("Core outbox request timed out", { cause: error });
      if (error instanceof CoreEventOutboxError) throw error;
      throw new CoreEventOutboxError("Core outbox request failed before receiving a valid response", { cause: error });
    } finally {
      clearTimeout(timeout);
    }
  }

  async function acknowledge(event, outcome, error) {
    const body = { leaseToken: event.leaseToken, outcome };
    if (error !== undefined) body.error = error;
    const { status, result } = await request(`/${event.id}/ack`, body);
    if (status !== 202 || result?.data?.accepted !== true || result?.data?.eventId !== event.id) {
      throw new CoreEventOutboxError("Core did not confirm the outbox acknowledgement", { status });
    }
    return Object.freeze({ duplicate: result.data.duplicate === true, outcome });
  }

  async function pollOnce() {
    if (polling) throw new CoreEventOutboxError("An outbox poll is already in progress");
    polling = true;
    try {
      const claimBody = { limit, leaseSeconds };
      if (eventTypes !== undefined) claimBody.eventTypes = [...eventTypes];
      const { status, result } = await request("/claim", claimBody);
      if (status !== 200 || !Array.isArray(result?.data)) {
        throw new CoreEventOutboxError("Core returned an invalid outbox claim response", { status });
      }
      if (result.data.length > 1) {
        throw new CoreEventOutboxError("Core returned more than one event for a single-event claim");
      }

      const summary = { claimed: result.data.length, delivered: 0, failed: 0, acknowledgementErrors: [] };
      for (const raw of result.data) {
        let event;
        try {
          event = requireClaimedEvent(raw, serverId);
        } catch (error) {
          // Without a trustworthy lease identity there is no safe acknowledgement to send.
          summary.acknowledgementErrors.push(Object.freeze({ eventId: raw?.id ?? null, message: safeErrorMessage(error, credential) }));
          continue;
        }

        let handlerError;
        try {
          await processEvent(event);
        } catch (error) {
          handlerError = safeErrorMessage(error, credential);
        }

        try {
          await acknowledge(event, handlerError === undefined ? "delivered" : "failed", handlerError);
          if (handlerError === undefined) summary.delivered += 1;
          else summary.failed += 1;
        } catch (error) {
          summary.acknowledgementErrors.push(Object.freeze({
            eventId: event.id,
            message: safeErrorMessage(error, credential)
          }));
        }
      }
      summary.acknowledgementErrors = Object.freeze(summary.acknowledgementErrors);
      return Object.freeze(summary);
    } finally {
      polling = false;
    }
  }

  return Object.freeze({ pollOnce });
}
